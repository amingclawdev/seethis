#define SEETHIS_INSPECTOR_MARK_NOTES_TESTING 1
#import "../src/platform/mac/overlay_mac.mm"
#include <cstdlib>
#include <iostream>
#include <map>
#include <stdexcept>

// This isolated overlay test does not link the production input adapter.
namespace seethis::platform {
EffectiveShortcutSnapshot InputShortcutStatus() { return {}; }
ChromeConnectionSnapshot ChromeConnectionStatus() { return {}; }
void ConnectChromeExplicit() {}
void RetryChromeExplicit() {}
bool OpenChromeAutomationSettingsExplicit() { return false; }
}  // namespace seethis::platform

static int checks=0;
static void Check(bool condition,const char* message) {
  ++checks;
  if(!condition)throw std::runtime_error(message);
}

// This focused fixture runs the real editor and NSTextView without ordering a
// window, changing the active app, posting input or touching any pasteboard.
@interface STUnshownNotePanel : STMarkNotePanel
@end
@implementation STUnshownNotePanel
- (void)makeKeyAndOrderFront:(id)sender {(void)sender;}
- (BOOL)makeFirstResponder:(NSResponder*)responder {(void)responder;return YES;}
- (void)orderOut:(id)sender {(void)sender;}
@end

static void CheckCloseCommitBoundary() {
  using Key=std::pair<std::string,std::string>;
  const Key first{"reference", "shot-one-mark"}, second{"reference", "shot-two-mark"};
  std::map<Key,std::string> saved{{first,"Published"},{second,"Second shot"}};
  std::vector<Key> writes;
  std::vector<std::string> copied;
  std::uint64_t revision=0;
  STNoteSaveStatus result=STNoteSaveStatus::kUpdated;
  std::function<void()> duringSave;
  STMarkNoteEditor* editor=[[STMarkNoteEditor alloc] initWithRead:
      [&](std::string_view ref,std::string_view mark)->STNoteReadResult {
        return {STNoteReadStatus::kReady,saved.at({std::string(ref),std::string(mark)}),revision};
      } save:[&](std::string_view ref,std::string_view mark,std::string_view text,
                 std::uint64_t expected)->STNoteSaveResult {
        const Key owner{std::string(ref),std::string(mark)};writes.push_back(owner);
        if(duringSave)duringSave();
        if(expected!=revision)return {STNoteSaveStatus::kConflict,revision};
        if(result==STNoteSaveStatus::kUpdated) {saved[owner]=text;++revision;}
        return {result,revision};
      } copy:[&](std::string_view ref)->STNoteCopyStatus {
        copied.emplace_back(ref);return STNoteCopyStatus::kCopied;
      }];
  STUnshownNotePanel* quiet=[[STUnshownNotePanel alloc]
      initWithContentRect:NSMakeRect(0,0,340,224)
      styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskNonactivatingPanel
      backing:NSBackingStoreBuffered defer:NO];
  quiet.releasedWhenClosed=NO;quiet.contentView=editor.panel.contentView;
  quiet.noteTextView=editor.textView;
  [editor setValue:quiet forKey:@"panel"];
  NSButton* close=[quiet standardWindowButton:NSWindowCloseButton];
  close.target=editor;close.action=@selector(complete:);
  const auto open=[&](const Key& owner) {
    [editor toggleReference:owner.first mark:owner.second number:owner==first?1:2 screenPoint:NSZeroPoint];
    Check(!editor.panel.isVisible,"focused fixture never presents a native window");
  };
  const auto type=[&](NSString* value) {
    editor.textView.string=value;
    [editor textDidChange:[NSNotification notificationWithName:NSTextDidChangeNotification object:editor.textView]];
  };
  open(first);type(@"Intermediate");
  [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.45]];
  Check(writes.empty() && !editor.saveTimer && saved[first]=="Published",
        "pause beyond the old debounce interval cannot publish the intermediate note");
  [editor.textView setMarkedText:@"组合" selectedRange:NSMakeRange(2,0) replacementRange:NSMakeRange(0,0)];
  Check(editor.textView.hasMarkedText && ![editor flushForAppend:first.first],
        "active IME composition is not committed by append");
  [editor saveNow];[editor retrySave:nil];
  Check(writes.empty() && editor.textView.hasMarkedText,"observation and unsolicited retry preserve uncommitted IME");
  [editor.textView unmarkText];type(@"Final 世界 📝");
  NSTimer* legacy=[NSTimer timerWithTimeInterval:1 repeats:NO block:^(NSTimer*){}];
  [editor autosaveTimer:legacy generation:editor.presentationGeneration];
  [editor saveNow];[editor retrySave:nil];
  Check(![editor saveDraft:YES] && !legacy.valid && writes.empty(),
        "legacy timers and direct save requests cannot bypass the close boundary");
  const auto generation=editor.presentationGeneration;
  [editor dismissOutsideForGeneration:generation];
  Check([editor flushForAppend:first.first] && [editor flushForAppend:"other"] && writes.empty(),
        "outside observations and append retain the exact draft without publishing");
  [editor hide];
  Check(!editor.presentationOpen && writes.empty(),"temporary retirement is not a committed close");
  open(first);
  Check([editor.textView.string isEqualToString:@"Final 世界 📝"] && editor.retryButton.hidden && writes.empty(),
        "reopening restores the in-memory draft without inventing a save failure");
  open(first);
  Check(!editor.presentationOpen && writes==std::vector<Key>{first} &&
        saved[first]=="Final 世界 📝" && copied==std::vector<std::string>{first.first},
        "number completion commits only the final text to its exact owner before copying");
  open(first);type(@"Abandoned intermediate");type(@"Final 世界 📝");
  Check(writes.size()==1,"edit then revert is still unpublished while editing");
  [close performClick:nil];
  Check(!editor.presentationOpen && writes.size()==2 && saved[first]=="Final 世界 📝",
        "X completion submits the final unchanged content; store no-op history is tested separately");
  open(first);type(@"New first final");
  const auto copyCount=copied.size();open(second);
  Check(editor.presentationOpen && editor.markID==second.second && writes.back()==first &&
        saved[first]=="New first final" && copied.size()==copyCount,
        "switching notes closes and saves the previous exact owner without copying");
  type(@"Second final");const auto beforeAppend=writes.size();
  Check([editor flushForAppend:first.first] && writes.size()==beforeAppend && saved[second]=="Second shot",
        "multi-shot editor retains the old saved note until its own close");
  // Append changes the aggregate revision. Preserve optimistic conflict
  // handling instead of silently overwriting an externally changed note.
  ++revision;[editor complete:nil];
  Check(editor.presentationOpen && !editor.retryButton.hidden && copied.size()==copyCount &&
        saved[second]=="Second shot","append revision conflict remains explicit and cannot report completion");
  [editor retrySave:nil];
  Check(!editor.presentationOpen && saved[second]=="Second final" && copied.size()==copyCount+1,
        "explicit retry closes the same second-shot owner with its retained final text");
  open(first);type(@"Local hidden draft");[editor hide];
  saved[first]="External change while hidden";++revision;open(first);
  const auto beforeHiddenClose=copied.size();[editor complete:nil];
  Check(editor.presentationOpen && copied.size()==beforeHiddenClose &&
        saved[first]=="External change while hidden" &&
        [editor.statusLabel.stringValue containsString:@"Changed elsewhere"],
        "retained draft keeps its base revision and cannot silently overwrite an edit made while hidden");
  [editor retrySave:nil];
  Check(!editor.presentationOpen && saved[first]=="Local hidden draft" && copied.size()==beforeHiddenClose+1,
        "only explicit conflict retry replaces the external edit with the retained final draft");
  for(const auto failure:{STNoteSaveStatus::kFailed,STNoteSaveStatus::kUnavailable,
                         STNoteSaveStatus::kConflict,STNoteSaveStatus::kCommitUncertain}) {
    open(first);type(@"Failed close draft");result=failure;
    const auto prior=saved[first];const auto copiesBefore=copied.size();
    [editor complete:nil];
    Check(editor.presentationOpen && !editor.retryButton.hidden && saved[first]==prior &&
          copied.size()==copiesBefore,"unsuccessful close retains the owner and cannot copy or claim completion");
    const auto attempts=writes.size();type(@"Changed after failure");result=STNoteSaveStatus::kUpdated;
    [editor retrySave:nil];[editor saveNow];
    Check(writes.size()==attempts && editor.presentationOpen,"typing cancels old close intent; retry cannot publish new editing text");
    [editor complete:nil];
    Check(!editor.presentationOpen && saved[first]=="Changed after failure","renewed explicit close saves the new final draft");
  }
  open(first);type(@"Switch final");result=STNoteSaveStatus::kFailed;open(second);
  Check(editor.markID==first.second && editor.presentationOpen,"failed switch retains original owner");
  result=STNoteSaveStatus::kUpdated;const auto switchCopies=copied.size();[editor retrySave:nil];
  Check(!editor.presentationOpen && saved[first]=="Switch final" && copied.size()==switchCopies,
        "switch-close retry ends the original edit without adding a clipboard effect");
  open(first);type(@"Submitted final");duringSave=[&]{type(@"New concurrent draft");};
  const auto beforeReentrant=copied.size();[editor complete:nil];duringSave={};
  Check(editor.presentationOpen && copied.size()==beforeReentrant &&
        [editor.textView.string isEqualToString:@"New concurrent draft"],
        "a stale save receipt cannot close a newer edit or copy its reference");
  const auto beforeDiscard=writes.size();[editor discardOwner];open(first);
  Check([editor.textView.string isEqualToString:@"Submitted final"] && writes.size()==beforeDiscard,
        "discard removes unpublished draft without recreating it or writing a history event");
  [editor hide];
}

// Component-only command target. Plain selected strings use a private named
// board; native delete/insert retain text editing and undo bookkeeping.
// These overrides never call super's clipboard actions.
// This proves responder routing, not the production general-pasteboard path.
@interface STNoteBoardTextView : NSTextView
@property(nonatomic,strong) NSPasteboard* fixtureBoard;
@end
@implementation STNoteBoardTextView
- (void)copy:(id)sender {
  (void)sender;
  Check(self.fixtureBoard && ![self.fixtureBoard.name isEqualToString:NSPasteboardNameGeneral],
        "component Copy requires its private named board");
  const NSRange selected=self.selectedRange;
  Check(NSMaxRange(selected)<=self.string.length,"component Copy has a valid native selection");
  if(!selected.length)return;
  NSString* plain=[self.string substringWithRange:selected];
  [self.fixtureBoard clearContents];
  Check([self.fixtureBoard setString:plain forType:NSPasteboardTypeString],
        "component Copy writes selected plain text to its named board");
}
- (void)cut:(id)sender {
  [self copy:sender];
  [self delete:sender];
}
- (void)paste:(id)sender {
  (void)sender;
  Check(self.fixtureBoard && ![self.fixtureBoard.name isEqualToString:NSPasteboardNameGeneral],
        "component Paste requires its private named board");
  NSString* plain=[self.fixtureBoard stringForType:NSPasteboardTypeString];
  Check(plain!=nil,"component Paste reads plain text from its named board");
  [self insertText:plain replacementRange:self.selectedRange];
}
@end

class NoteNamedClipboard {
 public:
  NoteNamedClipboard() : board([NSPasteboard pasteboardWithUniqueName]) {}
  ~NoteNamedClipboard() {
    [board releaseGlobally];
    std::cout<<"named-board END: releaseGlobally cleanup invoked\n";
  }
  void put(NSString* text) {
    [board clearContents];
    Check([board setString:text forType:NSPasteboardTypeString],"named-board synthetic payload written");
  }
  void copied(const std::function<void()>& command,NSString* expected) {
    command();
    Check([[board stringForType:NSPasteboardTypeString] isEqualToString:expected],
          "component Copy/Cut produces exactly the synthetic selection");
  }
  __strong NSPasteboard* board;
};

static NSEvent* NoteKeyEvent(NSWindow* window,NSString* character,
                            NSEventModifierFlags flags=NSEventModifierFlagCommand) {
  return [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint
      modifierFlags:flags timestamp:NSProcessInfo.processInfo.systemUptime
      windowNumber:window.windowNumber context:nil characters:character
      charactersIgnoringModifiers:character isARepeat:NO keyCode:0];
}
static void SendNoteKey(STMarkNoteEditor* editor,NSString* key,
                        NSEventModifierFlags flags=NSEventModifierFlagCommand) {
  Check(NSApp.keyWindow==editor.panel && editor.panel.isKeyWindow &&
        editor.panel.firstResponder==editor.textView,
        "key-event fixture requires the actual key note panel and text first responder");
  if([@[@"c",@"x",@"v"] containsObject:key.lowercaseString]) {
    Check([editor.textView isKindOfClass:STNoteBoardTextView.class],
          "clipboard key components require the explicit test-only receiver");
    STNoteBoardTextView* scoped=(STNoteBoardTextView*)editor.textView;
    Check(scoped.fixtureBoard && ![scoped.fixtureBoard.name isEqualToString:NSPasteboardNameGeneral],
          "clipboard key component receiver must own a private named board");
  }
  [NSApp sendEvent:NoteKeyEvent(editor.panel,key,flags)];
}
static void NoteMenuAction(STMarkNoteEditor* editor,SEL action) {
  Check([editor.textView isKindOfClass:STNoteBoardTextView.class] &&
        editor.panel.isKeyWindow && NSApp.keyWindow==editor.panel &&
        editor.panel.firstResponder==editor.textView,
        "menu component requires the key panel and exact test-only first responder");
  STNoteBoardTextView* scoped=(STNoteBoardTextView*)editor.textView;
  Check(scoped.fixtureBoard && ![scoped.fixtureBoard.name isEqualToString:NSPasteboardNameGeneral],
        "menu component receiver must own a private named board");
  // A synthetic menu with an explicit receiver avoids native menu construction
  // or validation reaching an inherited clipboard action. Interactive native
  // context-menu tracking remains a separate packaged-UI acceptance check.
  NSMenu* menu=[[NSMenu alloc] initWithTitle:@"Named-board component"];
  menu.autoenablesItems=NO;
  NSMenuItem* item=[[NSMenuItem alloc] initWithTitle:NSStringFromSelector(action)
      action:action keyEquivalent:@""];
  item.target=scoped;item.enabled=YES;[menu addItem:item];
  Check([NSApp targetForAction:action to:item.target from:item]==scoped &&
        editor.panel.firstResponder==scoped,
        "programmatic menu action resolves to the exact bound component receiver");
  [menu performActionForItemAtIndex:0];
  Check(editor.presentationOpen,"synthetic menu component action keeps note presentation open");
}

static void CheckNativeEditingWithoutClipboard() {
  std::map<std::string,std::string> notes{{"no-clipboard","Alpha 世界 📝 Beta"},
                                         {"other-owner","Other owner"}};
  STMarkNoteEditor* editor=[[STMarkNoteEditor alloc] initWithRead:
      [&](std::string_view ref,std::string_view)->STNoteReadResult {
        return {STNoteReadStatus::kReady,notes[std::string(ref)],0};
      } save:[&](std::string_view ref,std::string_view,std::string_view text,
                 std::uint64_t)->STNoteSaveResult {
        notes[std::string(ref)]=text;return {STNoteSaveStatus::kUpdated,1};
      }];
  [editor toggleReference:"no-clipboard" mark:"one" number:1 screenPoint:NSMakePoint(400,400)];
  @try {
    [editor.textView setSelectedRange:NSMakeRange(6,2)];
    std::cout<<"no-clipboard route: NSApplication sendEvent, keyPanel="<<editor.panel.isKeyWindow
             <<", textFirstResponder="<<(editor.panel.firstResponder==editor.textView)
             <<", appActive="<<NSApp.isActive<<", mainMenu="<<(NSApp.mainMenu!=nil)
             <<", selectionBefore="<<editor.textView.selectedRange.location
             <<":"<<editor.textView.selectedRange.length<<'\n';
    SendNoteKey(editor,@"a");
    const bool selected=NSEqualRanges(editor.textView.selectedRange,NSMakeRange(0,editor.textView.string.length));
    std::cout<<"CmdA selectionAfter="<<editor.textView.selectedRange.location
             <<":"<<editor.textView.selectedRange.length<<", expected=0:"
             <<editor.textView.string.length<<'\n';
    // Native insertion creates an undoable edit independently of CmdA's result.
    [editor.textView setSelectedRange:NSMakeRange(editor.textView.string.length,0)];
    [editor.textView insertText:@"追加" replacementRange:editor.textView.selectedRange];
    [editor.textView breakUndoCoalescing];
    Check(editor.textView.undoManager.canUndo,"native insertion provides a valid undo baseline");
    SendNoteKey(editor,@"z");
    const bool undone=[editor.textView.string isEqualToString:@"Alpha 世界 📝 Beta"];
    std::cout<<"CmdZ restoredSyntheticBase="<<undone<<'\n';
    Check(selected && undone,"NSApplication command route must dispatch CmdA and CmdZ to the focused note");
    // Redo through the native undo manager is selector/component coverage only.
    Check(editor.textView.undoManager.canRedo,"native undo retains a redo action");
    [editor.textView.undoManager redo];
    Check([editor.textView.string isEqualToString:@"Alpha 世界 📝 Beta追加"],
          "native redo restores the exact Unicode insertion");
    SendNoteKey(editor,@"z");[editor saveNow];
    Check(notes["no-clipboard"]=="Alpha 世界 📝 Beta","undo keeps the saved text unchanged until close");
    [editor toggleReference:"other-owner" mark:"one" number:1 screenPoint:NSMakePoint(400,400)];
    SendNoteKey(editor,@"z");
    Check([editor.textView.string isEqualToString:@"Other owner"] && !editor.textView.undoManager.canUndo,
          "same-number reference switch clears another owner's undo actions");
    for(const auto flags:{NSEventModifierFlagCommand|NSEventModifierFlagOption,
                         NSEventModifierFlagCommand|NSEventModifierFlagControl,
                         NSEventModifierFlagCommand|NSEventModifierFlagShift})
      Check(![editor.panel performKeyEquivalent:NoteKeyEvent(editor.panel,@"a",flags)],
            "stock note delegates unrelated command modifiers to AppKit");
    [editor.textView setMarkedText:@"组合输入" selectedRange:NSMakeRange(2,0)
                 replacementRange:NSMakeRange(0,0)];
    Check(![editor.panel performKeyEquivalent:NoteKeyEvent(editor.panel,@"a")] &&
          editor.textView.hasMarkedText,"stock note delegates marked composition without forcing commit");
    [editor.textView unmarkText];
    [editor.panel makeFirstResponder:nil];
    Check(![editor.panel performKeyEquivalent:NoteKeyEvent(editor.panel,@"a")],
          "stock note does not claim edit commands when another responder owns focus");
  } @finally {[editor hide];}
}

static void CheckNativeEditingOnNamedBoard() {
  NoteNamedClipboard clipboard;
  std::map<std::string,std::string> notes{{"A", "Alpha 世界 📝 Beta"},{"B", "Other owner"}};
  std::vector<std::pair<std::string,std::string>> writes;
  STMarkNoteEditor* editor=[[STMarkNoteEditor alloc] initWithRead:
      [&](std::string_view ref,std::string_view)->STNoteReadResult {
        return {STNoteReadStatus::kReady,notes[std::string(ref)],0};
      } save:[&](std::string_view ref,std::string_view,std::string_view text,
                 std::uint64_t)->STNoteSaveResult {
        notes[std::string(ref)]=text;writes.emplace_back(ref,text);
        return {STNoteSaveStatus::kUpdated,1};
      }];
  [editor toggleReference:"A" mark:"one" number:1 screenPoint:NSMakePoint(400,400)];
  // Explicit test-only rebinding: the production editor keeps stock NSTextView.
  NSTextView* original=editor.textView;
  NSScrollView* scroll=original.enclosingScrollView;
  STNoteBoardTextView* scoped=[[STNoteBoardTextView alloc] initWithFrame:original.frame];
  scoped.string=original.string;scoped.richText=NO;scoped.allowsUndo=YES;
  scoped.fixtureBoard=clipboard.board;scoped.delegate=editor;original.delegate=nil;
  [editor setValue:scoped forKey:@"textView"];scroll.documentView=scoped;
  editor.panel.noteTextView=scoped;
  Check([editor.panel makeFirstResponder:scoped] && editor.panel.firstResponder==scoped,
        "named-board fixture successfully focuses its explicit test-only receiver");
  @try {
    const NSRange selection=[editor.textView.string rangeOfString:@"世界 📝"];
    [editor.textView setSelectedRange:selection];
    clipboard.put(@"synthetic sentinel");
    // Synthetic menu component dispatch is separate from keyboard dispatch.
    // It does not claim native context-menu tracking or physical-key coverage.
    clipboard.copied([&]{NoteMenuAction(editor,@selector(copy:));},@"世界 📝");
    Check([editor.textView.string isEqualToString:@"Alpha 世界 📝 Beta"] &&
          NSEqualRanges(editor.textView.selectedRange,selection) && writes.empty(),
          "programmatic named-board menu Copy preserves text, selection and save state");
    clipboard.put(@"synthetic sentinel");
    std::cout<<"keyboard route: NSApplication sendEvent, keyPanel="<<editor.panel.isKeyWindow
             <<", textFirstResponder="<<(editor.panel.firstResponder==editor.textView)
             <<", appActive="<<NSApp.isActive<<", mainMenu="<<(NSApp.mainMenu!=nil)<<'\n';
    clipboard.copied([&]{SendNoteKey(editor,@"c");},@"世界 📝");
    Check(NSEqualRanges(editor.textView.selectedRange,selection) && writes.empty(),
          "CmdC keeps native selection and does not save");
    clipboard.copied([&]{SendNoteKey(editor,@"x");},@"世界 📝");
    Check([editor.textView.string isEqualToString:@"Alpha  Beta"] &&
          editor.textView.selectedRange.location==selection.location && !editor.saveTimer && writes.empty(),
          "CmdX cuts selected Unicode without publishing a draft");
    [editor.textView breakUndoCoalescing];
    SendNoteKey(editor,@"z");
    Check([editor.textView.string isEqualToString:@"Alpha 世界 📝 Beta"],
          "CmdZ restores exact native cut bytes");
    [editor.textView setSelectedRange:selection];
    clipboard.put(@"粘贴\nplain 🧭 text");
    SendNoteKey(editor,@"v");
    Check([editor.textView.string isEqualToString:@"Alpha 粘贴\nplain 🧭 text Beta"] &&
          !editor.textView.richText,"CmdV replaces the selected UTF-16 range with plain multiline Unicode");
    [editor.textView breakUndoCoalescing];SendNoteKey(editor,@"z");
    Check([editor.textView.string isEqualToString:@"Alpha 世界 📝 Beta"],
          "CmdZ reverses the complete paste as one edit");
    SendNoteKey(editor,@"a");
    Check(NSEqualRanges(editor.textView.selectedRange,NSMakeRange(0,editor.textView.string.length)),
          "CmdA selects the entire note");
    // Ordinary input and composition stay in NSTextView's native input path.
    SendNoteKey(editor,@"q",0);
    Check([editor.textView.string isEqualToString:@"q"],"ordinary keyDown replaces selection natively");
    [editor.textView breakUndoCoalescing];SendNoteKey(editor,@"z");
    Check([editor.textView.string isEqualToString:@"Alpha 世界 📝 Beta"],"native typing remains undoable");
    [editor.textView setSelectedRange:NSMakeRange(editor.textView.string.length,0)];
    [editor.textView setMarkedText:@"组合输入" selectedRange:NSMakeRange(2,0)
                 replacementRange:editor.textView.selectedRange];
    Check(editor.textView.hasMarkedText,"native composition fixture holds marked Chinese text");
    Check(![editor.panel performKeyEquivalent:NoteKeyEvent(editor.panel,@"c")],
          "marked composition delegates key equivalents to AppKit");
    Check(editor.textView.hasMarkedText,"command routing does not forcibly unmark composition");
    [editor.textView unmarkText];
    [editor.textView breakUndoCoalescing];
    [editor saveNow];
    Check(writes.empty(),"native mutations remain drafts until the edit is closed");
    [editor toggleReference:"B" mark:"one" number:1 screenPoint:NSMakePoint(400,400)];
    Check(writes.size()==1 && writes.back().first=="A" && writes.back().second==notes["A"],
          "switching note commits the previous owner's final text once");
    SendNoteKey(editor,@"z");
    Check([editor.textView.string isEqualToString:@"Other owner"] && !editor.textView.undoManager.canUndo,
          "switching references with the same number isolates native undo history");
    for(const auto flags:{NSEventModifierFlagCommand|NSEventModifierFlagOption,
                         NSEventModifierFlagCommand|NSEventModifierFlagControl,
                         NSEventModifierFlagCommand|NSEventModifierFlagShift})
      Check(![editor.panel performKeyEquivalent:NoteKeyEvent(editor.panel,@"a",flags)],
            "unrelated modified command chords fall through to AppKit");
    Check(![editor.panel performKeyEquivalent:NoteKeyEvent(editor.panel,@"k")],
          "unhandled command falls through to AppKit");
    NSTextField* unrelated=[NSTextField labelWithString:@"unrelated"];
    [editor.panel.contentView addSubview:unrelated];
    [editor.panel makeFirstResponder:nil];
    Check(![editor.panel performKeyEquivalent:NoteKeyEvent(editor.panel,@"a")],
          "an unfocused note never steals another responder's edit command");
    [editor.panel makeFirstResponder:editor.textView];
    [editor.textView setSelectedRange:NSMakeRange(0,5)];
    clipboard.copied([&]{NoteMenuAction(editor,@selector(cut:));},@"Other");
    Check([editor.textView.string isEqualToString:@" owner"],"programmatic named-board menu Cut mutates the selection");
    clipboard.put(@"Menu 世界\n");NoteMenuAction(editor,@selector(paste:));
    Check([editor.textView.string isEqualToString:@"Menu 世界\n owner"],"programmatic named-board menu Paste inserts plain Unicode");
  } @finally {
    [editor hide];
  }
}

@interface STTestOverlayRouting : NSObject <STOverlayRouting> {
 @public
  std::string calls;
  seethis::core::DisplayPoint last;
  bool badgeClaims;
  bool strokeClaims;
}
@end
@implementation STTestOverlayRouting
- (void)paintDisplay:(seethis::core::DisplayId)display {(void)display;}
- (BOOL)handleNoteBadgeClick:(seethis::core::DisplayPoint)point {
  last=point;calls+='B';return badgeClaims;
}
- (BOOL)click:(seethis::core::DisplayPoint)point {
  last=point;calls+='C';return strokeClaims;
}
- (BOOL)beginRegion:(seethis::core::DisplayPoint)point {
  last=point;calls+='D';return YES;
}
- (void)moveRegion:(seethis::core::DisplayPoint)point {
  last=point;calls+='M';
}
- (void)endRegion:(seethis::core::DisplayPoint)point {
  last=point;calls+='U';
}
@end

// Native dispatch fixture: a real AppKit window receives the exact original
// mouse event and performs its action while the local monitor observes it.
@interface STNoteOutsideTarget : NSView {
 @public
  int downs;
  NSEvent* received;
  std::function<void()> action;
}
@end
@implementation STNoteOutsideTarget
- (void)mouseDown:(NSEvent*)event {++downs;received=event;if(action)action();}
- (void)rightMouseDown:(NSEvent*)event {[self mouseDown:event];}
- (void)otherMouseDown:(NSEvent*)event {[self mouseDown:event];}
@end

static void DrainOutsideDismissals() {
  [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
}
static NSEvent* NoteMouseEvent(NSWindow* window,NSEventType type) {
  return [NSEvent mouseEventWithType:type location:NSMakePoint(20,20)
      modifierFlags:NSEventModifierFlagOption timestamp:NSProcessInfo.processInfo.systemUptime
      windowNumber:window.windowNumber context:nil eventNumber:27 clickCount:1 pressure:1];
}
// All completion paths below bind a typed synthetic copy callback. This fixture
// must never fall through to production CopyReferenceText or the general board.
static void CheckPersistentCompletion() {
  using Key=std::pair<std::string,std::string>;
  std::map<Key,std::string> notes;
  std::vector<std::string> calls;
  STNoteSaveStatus saveStatus=STNoteSaveStatus::kUpdated;
  STNoteReadStatus readStatus=STNoteReadStatus::kReady;
  STNoteCopyStatus copyStatus=STNoteCopyStatus::kCopied;
  bool saveThrows=false,copyThrows=false,readThrows=false;
  std::function<void()> duringSave,duringCopy;
  std::uint64_t revision=0;
  STMarkNoteEditor* editor=[[STMarkNoteEditor alloc] initWithRead:
    [&](std::string_view r,std::string_view m)->STNoteReadResult {
      if(readThrows)throw std::runtime_error("synthetic read fault");
      return {readStatus,notes[{std::string(r),std::string(m)}],revision};
    } save:[&](std::string_view r,std::string_view m,std::string_view text,
               std::uint64_t expected)->STNoteSaveResult {
      calls.push_back("save:"+std::string(r)+"/"+std::string(m)+":"+std::string(text));
      Check(expected<=revision,"save receives current known owner revision");
      if(duringSave)duringSave();
      if(saveThrows)throw std::runtime_error("synthetic save fault");
      if(saveStatus==STNoteSaveStatus::kUpdated || saveStatus==STNoteSaveStatus::kCommitUncertain) {
        notes[{std::string(r),std::string(m)}]=text;++revision;
      } else if(saveStatus==STNoteSaveStatus::kConflict)++revision;
      return {saveStatus,revision};
    } copy:[&](std::string_view r)->STNoteCopyStatus {
      calls.push_back("copy:"+std::string(r));
      Check(notes[{std::string(r),"one"}]=="Draft 世界 📝" || r=="B",
            "copy runs after the original owner's exact draft save");
      if(duringCopy)duringCopy();
      if(copyThrows)throw std::runtime_error("synthetic copy fault");
      return copyStatus;
    }];
  const auto open=[&](std::string r,std::string m="one") {
    [editor toggleReference:r mark:m number:1 screenPoint:NSMakePoint(400,400)];
  };
  const auto type=[&](NSString* text) {
    editor.textView.string=text;
    [editor textDidChange:[NSNotification notificationWithName:NSTextDidChangeNotification object:editor.textView]];
  };
  const auto copies=[&] {return std::count_if(calls.begin(),calls.end(),[](const auto& c){return c.rfind("copy:",0)==0;});};
  open("A");type(@"Draft 世界 📝");
  STMarkNotePanel* target=[[STMarkNotePanel alloc] initWithContentRect:NSMakeRect(30,30,120,120)
      styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskNonactivatingPanel backing:NSBackingStoreBuffered defer:NO];
  target.releasedWhenClosed=NO;
  STNoteOutsideTarget* view=[[STNoteOutsideTarget alloc] initWithFrame:NSMakeRect(0,0,120,120)];
  target.contentView=view;[target makeKeyAndOrderFront:nil];[target makeKeyWindow];
  Check(NSApp.keyWindow==target,"outside fixture establishes exact receiving key panel before testing focus preservation");
  const auto generation=editor.presentationGeneration;
  const auto selected=NSMakeRange(2,3);[editor.textView setSelectedRange:selected];
  for(const auto eventType:{NSEventTypeLeftMouseDown,NSEventTypeRightMouseDown,NSEventTypeOtherMouseDown}) {
    NSEvent* event=NoteMouseEvent(target,eventType);
    Check([editor observeLocalMouseDown:event generation:generation]==event,"outside observation preserves original event");
    [NSApp sendEvent:event];DrainOutsideDismissals();
    Check(editor.presentationOpen && editor.referenceID=="A" && editor.markID=="one" &&
          editor.presentationGeneration==generation && NSEqualRanges(selected,editor.textView.selectedRange) &&
          NSApp.keyWindow==target && copies()==0,"outside clicks preserve exact owner, selection and receiving app focus without copy");
  }
  Check(view->downs==3,"actual outside dispatch delivers each original event exactly once");
  [editor.panel cancelOperation:nil];[editor windowShouldClose:editor.panel];
  Check(editor.presentationOpen && calls.empty(),"ordinary Escape/generic AppKit close is not X completion or retirement");
  Check([editor textView:editor.textView doCommandBySelector:@selector(cancelOperation:)],
        "unmarked native Escape delegate is consumed without completion");
  [editor.textView setMarkedText:@"组合输入" selectedRange:NSMakeRange(2,0) replacementRange:NSMakeRange(0,0)];
  Check(editor.textView.hasMarkedText && ![editor textView:editor.textView doCommandBySelector:@selector(cancelOperation:)],
        "marked-text cancel remains with the native input client, component only");
  [editor.panel cancelOperation:nil];Check(editor.presentationOpen && copies()==0,"marked-text Escape cannot complete note");
  [editor.textView unmarkText];type(@"Draft 世界 📝");
  NSTimer* stale=editor.saveTimer;[editor saveNow];
  Check(calls.empty() && notes[{"A","one"}].empty(),"typing and legacy saveNow never publish an intermediate draft");
  calls.clear();open("A");
  Check(!editor.presentationOpen && calls==std::vector<std::string>{"save:A/one:Draft 世界 📝","copy:A"},
        "same exact pair confirms durable save then original ID copy then closes");
  for(const auto status:{STNoteSaveStatus::kFailed,STNoteSaveStatus::kUnavailable,
      STNoteSaveStatus::kConflict,STNoteSaveStatus::kCommitUncertain}) {
    open("A");type(@"Draft 世界 📝");saveStatus=status;calls.clear();
    [[editor.panel standardWindowButton:NSWindowCloseButton] performClick:nil];
    Check(editor.presentationOpen && copies()==0 && calls.size()==1 && !editor.retryButton.hidden &&
          [editor.textView.string isEqualToString:@"Draft 世界 📝"] &&
          ![editor.statusLabel.stringValue isEqualToString:@"Saved"],"explicit X save fault retains exact content and offers honest retry before any copy");
    const auto before=calls.size();open("B");
    Check(editor.referenceID=="A" && calls.size()==before+1 && copies()==0,
          "different reference with ordinal one cannot abandon failed A or copy");
    saveStatus=STNoteSaveStatus::kUpdated;[editor retrySave:nil];
    Check(!editor.presentationOpen && copies()==0,"failed switch retry closes its original owner without copy");
    open("A");open("A");
    Check(!editor.presentationOpen && calls.back()=="copy:A","renewed explicit same-pair completion saves then copies original A");
  }
  for(const auto status:{STNoteCopyStatus::kFailed,STNoteCopyStatus::kUnavailable}) {
    open("A");calls.clear();copyStatus=status;[editor complete:nil];
    Check(editor.presentationOpen && calls.size()==2 && [editor.statusLabel.stringValue containsString:@"Saved. Copy"] &&
          !editor.retryButton.hidden,"copy fault preserves saved note and stage-specific finish retry");
    const auto count=copies();[editor saveNow];[editor dismissOutsideForGeneration:generation];
    Check(copies()==count && editor.presentationOpen,"autosave/outside context never retries a failed clipboard effect");
    copyStatus=STNoteCopyStatus::kCopied;[editor retrySave:nil];
    Check(!editor.presentationOpen && copies()==count+1,"explicit Retry finish repeats save then copy and closes");
  }
  open("A");copyThrows=true;calls.clear();[editor complete:nil];
  Check(editor.presentationOpen && copies()==1 && [editor.statusLabel.stringValue containsString:@"Copy failed"],"throwing copy is retained callback fault, not physical clipboard proof");
  copyThrows=false;type(@"Draft 世界 📝");[editor retrySave:nil];
  Check(editor.presentationOpen && copies()==1,"new typing cancels pending finish so Retry save cannot copy");
  saveThrows=true;calls.clear();[editor complete:nil];
  Check(editor.presentationOpen && copies()==0 && [editor.statusLabel.stringValue containsString:@"failed"],"save exception cannot reach copy or close");
  saveThrows=false;[editor hide];
  for(const auto status:{STNoteReadStatus::kPending,STNoteReadStatus::kUnavailable}) {
    readStatus=status;saveStatus=STNoteSaveStatus::kUnavailable;open("A");calls.clear();[editor complete:nil];
    Check(editor.presentationOpen && calls.size()==1 && copies()==0,"clean pending/unavailable read is never durable save proof");
    [editor hide];
  }
  readThrows=true;open("A");calls.clear();[editor complete:nil];
  Check(editor.presentationOpen && copies()==0,"read exception fails honestly without copy");
  readThrows=false;saveStatus=STNoteSaveStatus::kUpdated;readStatus=STNoteReadStatus::kReady;
  calls.clear();open("B");
  Check(editor.referenceID=="B" && calls.size()==1 && calls[0].rfind("save:A/one:",0)==0 && copies()==0,
        "different reference same ordinal saves A before B and does not copy");
  [editor autosaveTimer:stale generation:generation];
  Check(calls.size()==1,"stale A operation cannot save B or overwrite its state");
  open("B","two");Check(editor.markID=="two" && copies()==0,"different stable mark same ordinal is a save-only switch");
  [editor hide];open("A");type(@"Draft 世界 📝");
  duringSave=[&]{[editor hide];};calls.clear();[editor complete:nil];duringSave={};
  Check(!editor.presentationOpen && copies()==0,"retirement inside save invalidates pending completion before clipboard");
  open("A");type(@"Draft 世界 📝");
  duringSave=[&]{type(@"New draft");};calls.clear();[editor complete:nil];duringSave={};
  Check(editor.presentationOpen && copies()==0 && [editor.textView.string isEqualToString:@"New draft"],"new revision inside save prevents stale receipt from completing");
  type(@"Draft 世界 📝");int validations=0;
  [editor setOwnerValidation:[&](std::string_view,std::string_view) {
    if(++validations==2)type(@"New unsaved draft");
    return true;
  }];
  calls.clear();[editor complete:nil];
  Check(validations==2 && copies()==0 && calls.size()==1 && notes[{"A","one"}]=="Draft 世界 📝" &&
        editor.presentationOpen && [editor.textView.string isEqualToString:@"New unsaved draft"] && !editor.saveTimer,
        "pre-copy validation edit invalidates saved draft receipt before any clipboard effect");
  [editor setOwnerValidation:STNoteOwnerValid{}];
  type(@"Draft 世界 📝");duringCopy=[&]{[editor hide];open("B");};calls.clear();[editor complete:nil];duringCopy={};
  Check(editor.presentationOpen && editor.referenceID=="B","late copy result cannot close a replacement owner");
  [editor hide];open("A");type(@"Draft 世界 📝");
  [editor setOwnerValidation:[](std::string_view,std::string_view){return false;}];calls.clear();[editor complete:nil];
  Check(!editor.presentationOpen && copies()==0,"invalid owner is retired without clipboard effect");
  [editor setOwnerValidation:STNoteOwnerValid{}];open("A");type(@"Draft 世界 📝");
  bool ownerAlive=true;
  [editor setOwnerValidation:[&](std::string_view,std::string_view){return ownerAlive;}];
  duringSave=[&]{ownerAlive=false;};calls.clear();[editor complete:nil];duringSave={};
  Check(!editor.presentationOpen && copies()==0,"owner invalidated during successful save cannot reach copy");
  [editor setOwnerValidation:STNoteOwnerValid{}];open("A");
  [editor setOwnerValidation:[&](std::string_view,std::string_view) {
    [editor hide];open("B");return false;
  }];
  calls.clear();[editor complete:nil];
  Check(editor.presentationOpen && editor.referenceID=="B" && copies()==0,
        "stale validation failure cannot retire a replacement owner");
  [editor setOwnerValidation:STNoteOwnerValid{}];[editor hide];
  [target orderOut:nil];[target close];
  // The two-argument fixture is fail-closed: a missing copy callback never uses
  // a global clipboard fallback even when save succeeds.
  STMarkNoteEditor* unavailable=[[STMarkNoteEditor alloc] initWithRead:
      [](std::string_view,std::string_view)->STNoteReadResult {return {STNoteReadStatus::kReady,"",0};}
      save:[](std::string_view,std::string_view,std::string_view,std::uint64_t)->STNoteSaveResult {return {STNoteSaveStatus::kUpdated,1};}];
  [unavailable toggleReference:"none" mark:"one" number:1 screenPoint:NSMakePoint(400,400)];
  [unavailable complete:nil];Check(unavailable.presentationOpen && [unavailable.statusLabel.stringValue containsString:@"Copy unavailable"],"absent typed copy callback cannot reach production clipboard");
  [unavailable hide];
}

// Editor seam only: this spy creates no panel/NSApp and never calls native UI,
// clipboard or persistence. The production coordinator validates every route.
@interface STP4UnshownEditor : STMarkNoteEditor {
 @public
  std::string root,mark;
  std::uint64_t generation;
  bool shown,switchAllowed;
  unsigned opens,completions;
}
@end
@implementation STP4UnshownEditor
- (instancetype)init {self=[super init];if(self)switchAllowed=true;return self;}
- (BOOL)presentationOpen {return shown;}
- (std::string)referenceID {return root;}
- (std::string)markID {return mark;}
- (std::uint64_t)presentationGeneration {return generation;}
- (void)openReference:(const std::string&)reference mark:(const std::string&)identifier
              number:(std::uint32_t)number screenPoint:(NSPoint)point {
  (void)number;(void)point;++opens;
  if(shown&&root==reference&&mark==identifier)return;
  if(shown&&!switchAllowed)return; // failed save-before-switch keeps its owner
  root=reference;mark=identifier;++generation;shown=true;
}
- (void)hide {shown=false;++generation;}
- (void)discardOwner {shown=false;root.clear();mark.clear();++generation;}
- (void)complete:(id)sender {(void)sender;++completions;shown=false;++generation;}
@end
static void CheckP4HistoricalInspectorEntry() {
  using namespace seethis::core;
  char pattern[]="/tmp/seethis-p4-local-note-XXXXXX";const std::filesystem::path directory=mkdtemp(pattern);
  {
    ReferenceStore store(directory);MarkController marks(store,[](const auto&){return true;});
    const std::string root(32,'a'),second(32,'b'),third(32,'c');
    FrozenDisplay captured;captured.uuid="obsolete";captured.id=1;captured.scale=1;captured.logical=captured.pixels={0,0,100,100};
    auto current=captured;current.uuid="current-inspector-screen";
    Reference first;first.id=root;first.context.displays={captured};first.regions.resize(1);
    auto next=first;next.first_mark_slot=1;
    ReferenceCollection collection;collection.status=MarkNotesStatus::kReady;
    collection.shots={{root,0,std::make_shared<const Reference>(first)},
      {second,1,std::make_shared<const Reference>(next)}};
    ReferenceJobSnapshot job;job.id=root;job.state=ReferenceJobState::kReady;job.context=first.context;
    MarkNotesSnapshot notes;notes.status=MarkNotesStatus::kReady;notes.revision=4;
    for(std::size_t slot=0;slot<2;++slot) {
      MarkSnapshot item;item.slot=slot;item.number=slot+1;item.id=StableMarkId(root,slot);
      item.shot_id=slot?second:root;item.anchor={1,CoordinateUnit::kLogicalPoints,{20,20},1};
      item.note=slot?"second unchanged":"historical exact draft";notes.marks.push_back(item);
    }
    GlobalVisibilitySnapshot visibility;visibility.state=GlobalVisibilityState::kVisible;
    STNoteVisibilitySource source;source.marks=&marks;
    source.lookup=[&](std::string_view){return std::optional<ReferenceJobSnapshot>(job);};
    source.snapshots=[&](std::string_view){return notes;};source.collection=[&](std::string_view){return collection;};
    source.displays=[&]{return std::vector<FrozenDisplay>{current};};source.visibility=[&]{return visibility;};
    source.screenReady=[]{return true;};source.foregroundPID=[]{return std::int64_t{0};};
    STP4UnshownEditor* editor=[STP4UnshownEditor new];
    STNoteVisibilityCoordinator* coordinator=[[STNoteVisibilityCoordinator alloc] initWithEditor:editor source:source];
    const auto a=notes.marks[0].id,b=notes.marks[1].id;
    seethis::platform::AppendCaptureMode mode;
    Check(mode.Enable(root,true),"explicit capture mode can bind independently of note editor");
    const auto modeGeneration=mode.generation;
    Check(![coordinator validReference:root mark:a],"obsolete topology cannot authorize a live note owner by default");
    Check([coordinator openInspectorReference:root mark:a number:1 screenPoint:NSMakePoint(10,10)]&&
      editor->root==root&&editor->mark==a&&editor->completions==0,"explicit historical Inspector entry reuses original owner without completion or copy");
    Check([coordinator validReference:root mark:a]&&!marks.LiveShot(root),"local editor admission ignores obsolete topology and creates no lease");
    const auto generation=editor->generation;
    Check([coordinator openInspectorReference:root mark:a number:1 screenPoint:NSMakePoint(10,10)]&&
      editor->generation==generation&&editor->completions==0,"repeated explicit edit focuses same owner without toggling finish");
    editor->switchAllowed=false;
    Check(![coordinator openInspectorReference:root mark:b number:2 screenPoint:NSMakePoint(10,10)]&&
      editor->mark==a&&[coordinator validReference:root mark:a],"failed save-before-switch retains original local admission and draft owner");
    editor->switchAllowed=true;
    Check([coordinator openInspectorReference:root mark:b number:2 screenPoint:NSMakePoint(10,10)]&&editor->mark==b,"successful owner switch remains explicit");
    auto latest=next;latest.first_mark_slot=2;collection.shots.push_back({third,2,std::make_shared<const Reference>(latest)});
    marks.RetireLiveShot();(void)seethis::platform::MarksForShot(notes,root);
    Check([coordinator validReference:root mark:b]&&editor->mark==b,"append preview and lease retirement cannot retarget persistent local owner");
    Check(mode.enabled()&&mode.root==root&&mode.generation==modeGeneration,
        "note owner/preview/retirement cannot consume or retarget explicit capture mode");
    [editor hide];
    Check([coordinator openInspectorReference:root mark:b number:2 screenPoint:NSMakePoint(12,12)]&&editor->mark==b,"closed historical note has an explicit reopen path");
    Check(![coordinator eligibleReference:root mark:b ownFocus:NO],"local note admission never authorizes historical live badge entry");
    Check(mode.enabled()&&editor->completions==0,"close/reopen never starts capture or completes/copies original note");
    visibility.state=GlobalVisibilityState::kInvisible;[coordinator reconcile];
    Check(!editor->shown&&![coordinator openInspectorReference:root mark:a number:1 screenPoint:NSMakePoint(10,10)],"global Hide still suppresses local editor entry");
    visibility.state=GlobalVisibilityState::kVisible;job.state=ReferenceJobState::kDeleted;
    Check(![coordinator openInspectorReference:root mark:a number:1 screenPoint:NSMakePoint(10,10)],"deleted root refuses historical reopen");
    job.state=ReferenceJobState::kExpired;
    Check(![coordinator openInspectorReference:root mark:a number:1 screenPoint:NSMakePoint(10,10)],"expired root refuses historical reopen");
    job.state=ReferenceJobState::kReady;notes.marks[0].shot_id="unknown";
    Check(![coordinator openInspectorReference:root mark:a number:1 screenPoint:NSMakePoint(10,10)]&&editor->completions==0,"unknown owning shot refuses without writing or finishing any editor");
  }
  std::filesystem::remove_all(directory);
}

int main(int argc,char** argv) {
  try {
    if(argc==2&&std::string_view(argv[1])=="--p4-component-only") {
      CheckP4HistoricalInspectorEntry();
      using namespace seethis::core;
      MarkNotesSnapshot all;all.status=MarkNotesStatus::kReady;all.revision=9;
      MarkSnapshot mark;mark.id=StableMarkId("root",7);mark.slot=7;mark.number=8;
      mark.note="original owner 世界";mark.shot_id="shot2";all.marks.push_back(mark);
      auto hidden=seethis::platform::MarksForShot(all,"shot1");
      auto restored=seethis::platform::MarksForShot(all,"shot2");
      Check(hidden.marks.empty()&&restored.marks.size()==1&&restored.marks[0].id==mark.id&&
          restored.marks[0].note==mark.note&&restored.revision==9,"preview filtering preserves original owner note and revision");
      Check(all.marks[0].id==mark.id&&all.marks[0].note==mark.note,"preview never mutates note owner");
      std::cout<<"P4 note owner pure component checks passed\n";return 0;
    }

  @autoreleasepool {
    [NSApplication sharedApplication];
    if(argc==2&&std::string_view(argv[1])=="--note-close-only") {
      CheckCloseCommitBoundary();
      std::cout<<"Note close boundary component checks passed: "<<checks<<" checks\n";return 0;
    }
    if(std::getenv("SEETHIS_NOTE_KEYBOARD_ONLY")) {
      [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
      Check(NSApp.mainMenu==nil,"editing fixture models the accessory app without an Edit main menu");
      CheckNativeEditingWithoutClipboard();
      std::cout<<"No-clipboard keyboard tests passed: "<<checks<<" checks\n";return 0;
    }
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    Check(NSApp.mainMenu==nil,"editing fixture models the accessory app without an Edit main menu");
    CheckNativeEditingWithoutClipboard();
    if(std::getenv("SEETHIS_NOTE_NAMED_BOARD"))CheckNativeEditingOnNamedBoard();
    else std::cout<<"Named-board editing component fixture SKIPPED\n";
    CheckPersistentCompletion();
    const NSPoint point=NSMakePoint(300,300);
    using namespace seethis::core;
    STNoteBadge badge{"ref","mark-1",1,
        {7,CoordinateUnit::kLogicalPoints,{100,200},2}};
    Check(STNoteBadgeHit(badge,{7,CoordinateUnit::kLogicalPoints,{100,200},2}),
          "first-point number center is clickable");
    Check(STNoteBadgeHit(badge,{7,CoordinateUnit::kBackingPixels,{200,400},2}),
          "backing-pixel input maps to the same number");
    Check(!STNoteBadgeHit(badge,{7,CoordinateUnit::kLogicalPoints,{113,213},2}) &&
          !STNoteBadgeHit(badge,{8,CoordinateUnit::kLogicalPoints,{100,200},2}) &&
          !STNoteBadgeHit(badge,{7,CoordinateUnit::kLogicalPoints,{140,200},2}),
          "empty, diagonal, and other-display hits pass through");
    STTestOverlayRouting* route=[STTestOverlayRouting new];
    NSWindow* window=[[NSWindow alloc]
        initWithContentRect:NSMakeRect(100,100,300,300)
        styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered
        defer:NO];
    STOverlayView* overlay=[[STOverlayView alloc]
        initWithFrame:NSMakeRect(0,0,300,300)];
    overlay.displayID=7;overlay.coordinator=route;
    window.contentView=overlay;
    NSEvent* down=[NSEvent mouseEventWithType:NSEventTypeLeftMouseDown
        location:NSMakePoint(100,200) modifierFlags:0 timestamp:0
        windowNumber:window.windowNumber context:nil eventNumber:1 clickCount:1
        pressure:1];
    route->badgeClaims=true;route->strokeClaims=false;
    [overlay mouseDown:down];
    Check(route->calls=="B" && overlay.claimedDeleteSequence &&
          route->last.display_id==7 && route->last.position.x==100 &&
          route->last.position.y==200,
          "production overlay mouse-down routes a numbered hit first");
    route->calls.clear();route->badgeClaims=false;route->strokeClaims=true;
    [overlay mouseDown:down];
    Check(route->calls=="BC",
          "production overlay routes a stroke before drawing");
    route->calls.clear();route->strokeClaims=false;
    [overlay mouseDown:down];
    Check(route->calls=="BCD",
          "production overlay begins a region only after misses");
    route->calls.clear();
    NSEvent* up=[NSEvent mouseEventWithType:NSEventTypeLeftMouseUp
        location:NSMakePoint(100,200) modifierFlags:0 timestamp:1
        windowNumber:window.windowNumber context:nil eventNumber:2 clickCount:1
        pressure:0];
    [overlay mouseUp:up];
    Check(route->calls=="U" && !overlay.claimedDeleteSequence,
          "production overlay clears its claimed sequence after release");

  }
  std::cout<<"Inspector numbered-note tests passed: "<<checks<<" checks\n";
  } catch(const std::exception& error) {
    std::cerr<<error.what()<<'\n';return 1;
  }
}
