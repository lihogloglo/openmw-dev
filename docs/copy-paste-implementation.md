# OpenMW-CS Copy-Paste Feature Implementation

**Feature**: Copy, Cut, and Paste functionality for OpenMW Construction Set
**Status**: In Development
**Started**: 2025-11-21
**Branch**: `claude/cs-copy-paste-feature-014Z889bU4f6ckzm3LXcAsT9`

---

## Table of Contents

1. [Architecture Analysis](#architecture-analysis)
2. [Implementation Plan](#implementation-plan)
3. [Implementation Status](#implementation-status)
4. [Design Decisions](#design-decisions)
5. [Testing Plan](#testing-plan)
6. [Known Issues](#known-issues)

---

## Architecture Analysis

### Overall Architecture

OpenMW-CS follows a clean **MVC pattern**:

- **Model**: `CSMWorld::Data` - Central data repository containing all game data collections
- **View**:
  - `CSVWorld::Table` - Table-based record editing interface
  - `CSVRender::WorldspaceWidget` - 3D scene editor for placing objects
- **Controller**: Command pattern via `QUndoCommand` subclasses
  - All modifications go through `QUndoStack` for undo/redo support
  - `CSMWorld::CommandDispatcher` coordinates commands with UI

### Key Components

#### 1. Command System (apps/opencs/model/world/commands.hpp/cpp)

All data modifications use Qt's undo framework:

```cpp
// Existing command classes we can leverage:
- CreateCommand     // Creates new records
- CloneCommand      // Duplicates records (extends CreateCommand)
- DeleteCommand     // Marks records as deleted
- ModifyCommand     // Modifies field values
- TouchCommand      // Brings base records into current plugin
```

**Key Insight**: `CloneCommand` already implements record duplication with field override support. We can leverage this for paste operations.

#### 2. Data Model (apps/opencs/model/world/)

```cpp
// Central data store
class Data : public QObject {
    IdCollection<ESM::Global> mGlobals;
    RefIdCollection mReferenceables;  // Objects (items, NPCs, etc.)
    RefCollection mRefs;              // Object instances in cells
    // ... many other collections
};

// Reference collection handles cell instances
class RefCollection : public Collection<CellRef> {
    std::string getNewId();  // Generates unique IDs like "0xABCD1234"
    void cloneRecord(...);   // Duplicates a reference
};
```

**Record States**:
- `State_BaseOnly` - Unmodified base game record
- `State_Modified` - Base record with modifications
- `State_ModifiedOnly` - New record not in base game
- `State_Deleted` - Marked for deletion

#### 3. Selection System

**Table View** (apps/opencs/view/world/table.hpp/cpp):
- Uses Qt's `QItemSelectionModel` for row selection
- `getSelectedIds()` returns `std::vector<std::string>` of selected record IDs
- Supports multi-selection with Ctrl/Shift

**3D Scene** (apps/opencs/view/render/instanceselectionmode.hpp/cpp):
- `InstanceSelectionMode` handles object picking in 3D viewport
- Selection stored in scene graph via `TagBase` references
- `getSelection(elementMask)` returns selected object tags

**Command Dispatcher** (apps/opencs/model/world/commanddispatcher.hpp):
- Bridges UI and command system
- Tracks current selection via `setSelection()`
- Provides slots for UI actions: `executeDelete()`, `executeRevert()`, etc.

#### 4. UI Actions (apps/opencs/view/world/table.cpp)

Current actions in table view:
```cpp
mEditAction      // Open record editor
mCreateAction    // Create new record
mCloneAction     // Clone selected record (single only)
mDeleteAction    // Delete selected records
mRevertAction    // Revert modifications
mTouchAction     // Touch base records
```

Keyboard shortcuts via `CSMPrefs::Shortcut` system:
- "table-edit", "table-add", "table-clone", "table-remove", etc.
- We'll add: "table-copy", "table-cut", "table-paste"

### Relevant Files Map

| Component | File Path | Purpose |
|-----------|-----------|---------|
| Commands | `apps/opencs/model/world/commands.hpp/cpp` | Command class definitions |
| Data Store | `apps/opencs/model/world/data.hpp/cpp` | Central data repository |
| Table View | `apps/opencs/view/world/table.hpp/cpp` | Table UI with actions |
| Command Dispatcher | `apps/opencs/model/world/commanddispatcher.hpp/cpp` | UI-Command bridge |
| Reference Collection | `apps/opencs/model/world/refcollection.hpp/cpp` | Cell instance handling |
| 3D Selection | `apps/opencs/view/render/instanceselectionmode.hpp/cpp` | 3D object selection |
| Document | `apps/opencs/model/doc/document.hpp/cpp` | Document with undo stack |

---

## Implementation Plan

### Phase 1: Clipboard Infrastructure ✅ NEXT

**Goal**: Create clipboard storage system in `CSMWorld::Data`

**Files to Modify**:
- `apps/opencs/model/world/data.hpp`
- `apps/opencs/model/world/data.cpp`

**Changes**:
1. Add member variables:
   ```cpp
   std::vector<std::unique_ptr<RecordBase>> mClipboard;
   UniversalId::Type mClipboardType;
   bool mClipboardIsCut;
   ```

2. Add methods:
   ```cpp
   void copyToClipboard(const std::vector<std::string>& ids,
                        UniversalId::Type type, bool isCut);
   const std::vector<std::unique_ptr<RecordBase>>& getClipboard() const;
   UniversalId::Type getClipboardType() const;
   bool hasClipboard() const;
   bool isClipboardCut() const;
   void clearClipboard();
   ```

**Design Decisions**:
- Clipboard scope: Per-document (prevents data inconsistencies)
- Store full record clones (ensures clipboard persists after source deletion)
- Track type to validate paste compatibility

### Phase 2: Copy/Cut/Paste Commands

**Goal**: Implement command classes for clipboard operations

**Files to Modify**:
- `apps/opencs/model/world/commands.hpp`
- `apps/opencs/model/world/commands.cpp`

**New Classes**:

#### 2.1 CopyCommand
```cpp
class CopyCommand : public QUndoCommand {
public:
    CopyCommand(Data& data, const std::vector<std::string>& ids,
                UniversalId::Type type, QUndoCommand* parent = nullptr);
    void redo() override;  // Copy records to clipboard
    void undo() override;  // No-op (doesn't modify data)
};
```

#### 2.2 CutCommand
```cpp
class CutCommand : public QUndoCommand {
public:
    CutCommand(Data& data, IdTable& table,
               const std::vector<std::string>& ids,
               UniversalId::Type type, QUndoCommand* parent = nullptr);
    void redo() override;  // Copy + delete
    void undo() override;  // Restore deleted records
private:
    // Child commands: CopyCommand + DeleteCommand(s)
};
```

#### 2.3 PasteCommand
```cpp
class PasteCommand : public QUndoCommand {
public:
    PasteCommand(Data& data, IdTable& table,
                 UniversalId::Type targetType,
                 QUndoCommand* parent = nullptr);
    void redo() override;  // Clone records from clipboard
    void undo() override;  // Remove pasted records
private:
    // Uses CloneCommand for each clipboard record
};
```

**Design Decisions**:
- Copy: Non-modifying, doesn't affect undo stack visibility
- Cut: Composite command (copy + delete), fully undoable
- Paste: Creates new records with auto-generated IDs via `RefCollection::getNewId()`

### Phase 3: Table View Integration

**Goal**: Add copy/paste UI to table view

**Files to Modify**:
- `apps/opencs/view/world/table.hpp`
- `apps/opencs/view/world/table.cpp`
- `apps/opencs/model/world/commanddispatcher.hpp`
- `apps/opencs/model/world/commanddispatcher.cpp`

**Changes**:

#### 3.1 Table Actions (table.hpp)
```cpp
QAction* mCopyAction;
QAction* mCutAction;
QAction* mPasteAction;
```

#### 3.2 Table Constructor (table.cpp)
Add after existing action creation (~line 330):
```cpp
// Copy action
mCopyAction = new QAction(tr("Copy Record"), this);
connect(mCopyAction, &QAction::triggered, this, &Table::copyRecord);
mCopyAction->setIcon(Misc::ScalableIcon::load(":edit-copy"));
addAction(mCopyAction);
CSMPrefs::Shortcut* copyShortcut = new CSMPrefs::Shortcut("table-copy", this);
copyShortcut->associateAction(mCopyAction);

// Cut action
mCutAction = new QAction(tr("Cut Record"), this);
connect(mCutAction, &QAction::triggered, this, &Table::cutRecord);
mCutAction->setIcon(Misc::ScalableIcon::load(":edit-cut"));
addAction(mCutAction);
CSMPrefs::Shortcut* cutShortcut = new CSMPrefs::Shortcut("table-cut", this);
cutShortcut->associateAction(mCutAction);

// Paste action
mPasteAction = new QAction(tr("Paste Record"), this);
connect(mPasteAction, &QAction::triggered, this, &Table::pasteRecord);
mPasteAction->setIcon(Misc::ScalableIcon::load(":edit-paste"));
addAction(mPasteAction);
CSMPrefs::Shortcut* pasteShortcut = new CSMPrefs::Shortcut("table-paste", this);
pasteShortcut->associateAction(mPasteAction);
```

#### 3.3 Context Menu (table.cpp)
Add to `contextMenuEvent()` (~line 90):
```cpp
if (selectedRows.size() > 0)
{
    menu.addAction(mCopyAction);
    if (!mEditLock)
        menu.addAction(mCutAction);
}

if (mDocument.getData().hasClipboard() && !mEditLock)
{
    menu.addAction(mPasteAction);
}
menu.addSeparator();
```

#### 3.4 CommandDispatcher Methods
```cpp
void executeCopy();
void executeCut();
void executePaste();
bool canPaste() const;  // Check clipboard compatibility
```

### Phase 4: 3D Scene Integration

**Goal**: Add copy/paste to 3D scene editor

**Files to Modify**:
- `apps/opencs/view/render/instanceselectionmode.hpp`
- `apps/opencs/view/render/instanceselectionmode.cpp`

**Changes**:

#### 4.1 Add Actions (instanceselectionmode.hpp)
```cpp
QAction* mCopySelection;
QAction* mCutSelection;
QAction* mPasteSelection;
```

#### 4.2 Context Menu (instanceselectionmode.cpp)
Extend `createContextMenu()`:
```cpp
menu->addAction(mCopySelection);
menu->addAction(mCutSelection);
if (hasClipboard)
    menu->addAction(mPasteSelection);
menu->addSeparator();
menu->addAction(mDeleteSelection);
```

#### 4.3 Position Offset Logic
For pasted instances:
- **Option A**: Fixed offset (+16 units X, +16 units Y)
- **Option B**: Paste at cursor position (use `WorldspaceWidget::mousePick()`)
- **Option C**: User preference

**Initial Implementation**: Option A (simplest, most predictable)

### Phase 5: Keyboard Shortcut Configuration

**Goal**: Register shortcuts in preferences system

**Files to Modify**:
- `apps/opencs/model/prefs/shortcutmanager.cpp` (or equivalent)

**Shortcuts**:
- `table-copy` → Ctrl+C (copy in table view)
- `table-cut` → Ctrl+X (cut in table view)
- `table-paste` → Ctrl+V (paste in table view)
- `scene-copy` → Ctrl+C (copy in 3D scene)
- `scene-cut` → Ctrl+X (cut in 3D scene)
- `scene-paste` → Ctrl+V (paste in 3D scene)

### Phase 6: Advanced Features (Optional)

**Potential Enhancements**:
1. **Duplicate shortcut** (Ctrl+D): Clone in place without clipboard
2. **Paste Special dialog**: Paste with modifications/offset options
3. **Cross-cell paste**: Update cell field when pasting to different cell
4. **Visual paste preview**: Ghost preview of objects before confirming paste
5. **Clipboard persistence**: Save clipboard across app restarts

---

## Implementation Status

### Current Phase: Phase 2 - Commands

### Phase 1: Clipboard Infrastructure ✅ COMPLETE

| Task | Status | Notes |
|------|--------|-------|
| Create design document | ✅ Done | This document |
| Add clipboard members to Data | ✅ Done | Commit e2b3101 |
| Implement copyToClipboard() | ✅ Done | Commit e2b3101 |
| Implement getClipboard() | ✅ Done | Commit e2b3101 |
| Implement clipboard utility methods | ✅ Done | Commit e2b3101 |
| Initialize clipboard in constructor | ✅ Done | Commit e2b3101 |

### Phase 2: Commands

| Task | Status | Notes |
|------|--------|-------|
| Implement CopyCommand | 🔄 In Progress | |
| Implement CutCommand | 🔲 Not Started | |
| Implement PasteCommand | 🔲 Not Started | |
| Test single object copy-paste | 🔲 Not Started | |
| Test multi-object copy-paste | 🔲 Not Started | |

### Phase 3: Table Integration

| Task | Status | Notes |
|------|--------|-------|
| Add actions to Table | 🔲 Not Started | |
| Add keyboard shortcuts | 🔲 Not Started | |
| Update context menu | 🔲 Not Started | |
| Implement CommandDispatcher methods | 🔲 Not Started | |
| Test table copy-paste workflow | 🔲 Not Started | |

### Phase 4: 3D Scene Integration

| Task | Status | Notes |
|------|--------|-------|
| Add actions to InstanceSelectionMode | 🔲 Not Started | |
| Implement position offset logic | 🔲 Not Started | |
| Update 3D context menu | 🔲 Not Started | |
| Test 3D copy-paste workflow | 🔲 Not Started | |

### Phase 5: Shortcuts & Polish

| Task | Status | Notes |
|------|--------|-------|
| Register shortcuts in prefs | 🔲 Not Started | |
| Add icons for actions | 🔲 Not Started | |
| Test all shortcuts | 🔲 Not Started | |
| Update user documentation | 🔲 Not Started | |

---

## Design Decisions

### Decision Log

| # | Decision | Rationale | Date |
|---|----------|-----------|------|
| 1 | Clipboard scope: Per-document | Prevents data inconsistencies when working with multiple plugins | 2025-11-21 |
| 2 | Store full record clones in clipboard | Ensures clipboard persists even if source is deleted | 2025-11-21 |
| 3 | Track clipboard type (UniversalId::Type) | Enables validation when pasting to prevent type mismatches | 2025-11-21 |
| 4 | Track cut vs copy mode | Allows proper handling of cut operation (mark for deletion) | 2025-11-21 |
| 5 | Use existing CloneCommand for paste | Leverages tested infrastructure for record duplication | 2025-11-21 |
| 6 | Fixed position offset for 3D paste (initial) | Simpler than cursor positioning, more predictable | 2025-11-21 |
| 7 | Auto-generate IDs for pasted records | Prevents ID conflicts, uses RefCollection::getNewId() | 2025-11-21 |

### Open Questions

| # | Question | Options | Status | Resolution |
|---|----------|---------|--------|------------|
| 1 | Should copy be visible in undo history? | A) Yes B) No | ✅ Resolved | No - copy doesn't modify data |
| 2 | Icon resources location? | A) Use Qt standard B) Custom icons C) Existing OpenCS icons | 🔲 Open | TBD - check resources directory |
| 3 | 3D paste position strategy? | A) Fixed offset B) Cursor position C) User preference | ✅ Resolved | Start with A, add B later |
| 4 | Cross-type paste handling? | A) Block with error B) Allow with conversion C) Prompt user | 🔲 Open | TBD - likely A for simplicity |
| 5 | Multi-cell paste behavior? | A) Paste all to current cell B) Preserve cell references | 🔲 Open | TBD - depends on use case |

---

## Testing Plan

### Unit Tests

1. **Clipboard Operations**
   - Copy single record → verify clipboard contains 1 record
   - Copy multiple records → verify clipboard contains N records
   - Cut record → verify clipboard contains record AND record marked deleted
   - Clear clipboard → verify clipboard is empty

2. **Paste Operations**
   - Paste single record → verify new record created with unique ID
   - Paste multiple records → verify N new records created
   - Paste after cut → verify original deleted, new created
   - Undo paste → verify pasted records removed
   - Redo paste → verify records re-created

3. **ID Generation**
   - Paste 100 records → verify all IDs unique
   - Paste across documents → verify no ID conflicts

### Integration Tests

1. **Table View Workflow**
   - Select record in table → Ctrl+C → Ctrl+V → verify new row appears
   - Multi-select in table → copy → paste → verify multiple rows created
   - Cut record → paste in different table (same type) → verify transfer
   - Copy record → switch document → paste → verify works cross-document

2. **3D Scene Workflow**
   - Select object in 3D → Ctrl+C → Ctrl+V → verify duplicate at offset position
   - Select multiple objects → copy → paste → verify all duplicated
   - Copy in 3D → paste in different cell → verify objects appear

3. **Mixed Workflow**
   - Copy in table → paste in 3D scene → verify works (if same type)
   - Copy in 3D → paste in table → verify works

4. **Undo/Redo**
   - Copy → Paste → Undo → verify pasted records removed
   - Copy → Paste → Undo → Redo → verify records reappear
   - Cut → Paste → Undo → verify original restored
   - Complex sequence: Create → Copy → Paste → Delete → Undo chain

### Edge Cases

1. **Empty Selection**: Copy with no selection → verify no crash, clipboard unchanged
2. **Invalid Type Paste**: Copy NPC → paste in Items table → verify error or rejection
3. **Clipboard Persistence**: Copy → close document → reopen → verify clipboard cleared
4. **Large Selection**: Copy 1000 objects → paste → verify performance acceptable
5. **Deleted Record Copy**: Copy deleted record → verify copied or error message
6. **Base Game Record**: Copy base game record → paste → verify creates modified copy

### Manual Testing Checklist

- [ ] Copy single record via context menu
- [ ] Copy single record via Ctrl+C
- [ ] Copy multiple records (5-10)
- [ ] Cut single record
- [ ] Cut multiple records
- [ ] Paste single record via context menu
- [ ] Paste single record via Ctrl+V
- [ ] Paste multiple records
- [ ] Copy in table, paste in 3D scene
- [ ] Copy in 3D scene, paste in table
- [ ] Verify undo/redo works for all operations
- [ ] Verify shortcuts appear in preferences
- [ ] Verify icons display correctly
- [ ] Verify status messages (e.g., "5 records copied")

---

## Known Issues

*To be populated as issues are discovered during implementation*

### Bugs

| # | Description | Severity | Status | Workaround |
|---|-------------|----------|--------|------------|
| | | | | |

### Limitations

| # | Description | Impact | Future Enhancement? |
|---|-------------|--------|---------------------|
| | | | |

---

## References

### Code Locations

- **Undo System**: Qt documentation on QUndoCommand, QUndoStack
- **Existing Clone**: `apps/opencs/model/world/commands.cpp:390` (CloneCommand::redo)
- **ID Generation**: `apps/opencs/model/world/refcollection.cpp` (getNewId method)
- **Table Actions**: `apps/opencs/view/world/table.cpp:254` (Table constructor)
- **Context Menu**: `apps/opencs/view/world/table.cpp:52` (contextMenuEvent)

### Similar Features

- **Clone Record**: Single-record duplication (we extend this to multi-record)
- **Touch Record**: Brings base records into plugin (similar workflow to paste)
- **Extended Delete**: Multi-selection deletion (similar selection handling)

---

## Development Notes

### Session Log

**2025-11-21 - Initial Analysis & Planning**
- Completed comprehensive codebase analysis
- Identified existing CloneCommand infrastructure to leverage
- Documented architecture and key components
- Created phased implementation plan
- Next: Begin Phase 1 - Clipboard Infrastructure

---

## Future Enhancements

Ideas for future iterations:

1. **Smart Paste**: Analyze clipboard contents and offer type conversion (e.g., paste NPC template as actual NPC instance)
2. **Clipboard History**: Remember last N clipboard operations
3. **Paste Transformation Dialog**: UI to adjust position, rotation, scale before pasting
4. **Cross-Plugin Paste**: Handle dependency resolution when pasting between plugins
5. **Format Preservation**: Maintain cell references, script attachments, etc. when pasting
6. **Batch Operations**: Paste with automatic array/grid layout
7. **Export/Import Clipboard**: Save clipboard to file for sharing snippets

---

*This document is a living reference and should be updated as implementation progresses.*
