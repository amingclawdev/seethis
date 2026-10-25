#if !defined(SEETHIS_NCV_FULL_COORDINATOR_TESTING)
#define SEETHIS_INSPECTOR_MARK_NOTES_TESTING 1
#endif
#import "../src/platform/mac/overlay_mac.mm"
#include <cstdlib>
#include <iostream>
#include <map>

// This isolated overlay test does not link the production input adapter.
namespace seethis::platform {
EffectiveShortcutSnapshot InputShortcutStatus() { return {}; }
#if !defined(SEETHIS_NCV_FULL_COORDINATOR_TESTING)
ChromeConnectionSnapshot ChromeConnectionStatus() { return {}; }
void ConnectChromeExplicit() {}
void RetryChromeExplicit() {}
bool OpenChromeAutomationSettingsExplicit() { return false; }
#endif
}  // namespace seethis::platform

static int checks=0;
static void Check(bool condition,const char* message) {
  ++checks;
  if(!condition){std::cerr<<message<<'\n';std::exit(1);}
}

// Keyed metadata replaces persistence only. All eligibility, publication,
// generation acceptance and presentation decisions are production code.
struct VisibilityFixture {
  using Key=std::pair<std::string,std::string>;
  struct Write {Key key;std::string note;std::uint64_t revision;};
  std::filesystem::path directory;
  std::unique_ptr<seethis::core::ReferenceStore> store;
  std::unique_ptr<seethis::core::SettingsStore> settings;
  std::unique_ptr<seethis::core::MarkController> marks;
  std::map<std::string,seethis::core::ReferenceJobSnapshot> jobs;
  std::map<std::string,seethis::core::MarkNotesSnapshot> snapshots;
  std::vector<seethis::core::FrozenDisplay> displays;
  std::vector<Write> writes;
  std::vector<std::string> copies;
  std::int64_t foreground=10;
  bool permission=true;
  STNoteSaveStatus saveStatus=STNoteSaveStatus::kUpdated;
  STMarkNoteEditor* editor;
  STNoteVisibilityCoordinator* visibility;
  const std::string a=std::string(32,'a'),b=std::string(32,'b');
  VisibilityFixture() {
    using namespace seethis::core;
    char path[]="/tmp/seethis-ncv-XXXXXX";directory=mkdtemp(path);
    store=std::make_unique<ReferenceStore>(directory);
    settings=std::make_unique<SettingsStore>(directory/"settings.json");
    marks=std::make_unique<MarkController>(*store,[](const StoredReference&){return false;});
    FrozenDisplay display;display.uuid="display";display.id=1;display.scale=1;
    display.logical={0,0,100,100};display.pixels={0,0,100,100};
    display.logical_to_pixels={1,-1,0,100};displays={display};
    for(const auto& id:{a,b}) {
      ReferenceJobSnapshot job;job.id=id;job.state=ReferenceJobState::kReady;
      job.context.pid=job.context.window_pid=id==a?10:20;
      job.context.bundle_id=id==a?"app.A":"app.B";
      job.context.window_id=id==a?101:202;job.context.window={0,0,100,100};
      job.context.displays=displays;job.context.self_pid=99;
      job.context.app_name="Synthetic fixture";job.context.executable="/synthetic";
      job.context.quartz_to_appkit_top=100;job.context.observed=Now();
      job.context.selection_method="frontmost-pid/topmost-normal-containing-initial-point";
      job.context.exclusion_method="exact-window-allowlist;self-pid-rejected;child-windows-off";
      job.context.excluded_window_ids={700};
      job.context.window_title=id==a?"Synthetic A":"Synthetic B";
      job.context.title={1,TitleAvailability::kReady};jobs[id]=job;
      MarkNotesSnapshot notes;notes.status=MarkNotesStatus::kReady;
      for(std::size_t slot=0;slot<2;++slot)
        notes.marks.push_back({StableMarkId(id,slot),slot,static_cast<std::uint32_t>(slot+1),
            {1,CoordinateUnit::kLogicalPoints,{10.+slot*20,10},1},{},"",true,0});
      snapshots[id]=notes;
    }
    editor=[[STMarkNoteEditor alloc] initWithRead:
      [&](std::string_view ref,std::string_view mark)->STNoteReadResult {
        auto& snapshot=snapshots.at(std::string(ref));
        for(const auto& item:snapshot.marks)if(item.id==mark)
          return {snapshot.status==MarkNotesStatus::kPending?STNoteReadStatus::kPending:
              STNoteReadStatus::kReady,item.note,snapshot.revision};
        return {};
      } save:[&](std::string_view ref,std::string_view mark,std::string_view note,
                  std::uint64_t revision)->STNoteSaveResult {
        writes.push_back({{std::string(ref),std::string(mark)},std::string(note),revision});
        auto& snapshot=snapshots.at(std::string(ref));
        if(saveStatus==STNoteSaveStatus::kUpdated || saveStatus==STNoteSaveStatus::kCommitUncertain) {
          ++snapshot.revision;
          for(auto& item:snapshot.marks)if(item.id==mark)item.note=note;
        } else if(saveStatus==STNoteSaveStatus::kConflict)++snapshot.revision;
        return {saveStatus,snapshot.revision};
      } copy:[&](std::string_view ref)->STNoteCopyStatus {
        copies.emplace_back(ref);return STNoteCopyStatus::kCopied;
      }];
    STNoteVisibilitySource source;source.marks=marks.get();
    source.visibility=[&]{return settings->Visibility();};
    source.lookup=[&](std::string_view id)->std::optional<ReferenceJobSnapshot> {
      const auto found=jobs.find(std::string(id));
      return found==jobs.end()?std::nullopt:std::optional(found->second);
    };
    source.snapshots=[&](std::string_view id){return snapshots.at(std::string(id));};
    source.displays=[&]{return displays;};source.foregroundPID=[&]{return foreground;};
    source.screenReady=[&]{return permission;};
    visibility=[[STNoteVisibilityCoordinator alloc] initWithEditor:editor source:std::move(source)];
    Window(a);
  }
  ~VisibilityFixture() {
    [visibility retire];visibility=nil;editor=nil;marks.reset();store.reset();
    std::filesystem::remove_all(directory);
  }
  std::string Mark(const std::string& id,std::size_t slot=0) {
    return seethis::core::StableMarkId(id,slot);
  }
  seethis::core::WindowObservation Observation(const std::string& id) {
    const auto& context=jobs.at(id).context;
    return {seethis::core::WindowAvailability::kReady,marks->window_observation().generation+1,
        seethis::core::Now(),context.window_pid,context.bundle_id,context.window_id,context.window,context.native_window,context.window_title,
        {1,seethis::core::TitleAvailability::kReady}};
  }
  void Window(const std::string& id) {
    const auto observation=Observation(id);foreground=observation.pid;
    [visibility publishWindow:observation];
  }
  void Open(const std::string& id,std::size_t slot=0) {
    [visibility toggleReference:id mark:Mark(id,slot) number:1 screenPoint:NSMakePoint(300,300)];
  }
  void Type(NSString* text) {
    editor.textView.string=text;
    [editor textDidChange:[NSNotification notificationWithName:NSTextDidChangeNotification object:editor.textView]];
  }
  seethis::core::PageIdentity Page(const std::string& id,std::string tab) {
    const auto& context=jobs.at(id).context;
    seethis::core::PageIdentity identity;
    identity.browser_pid=context.window_pid;identity.window_id=context.window_id;
    identity.window_bounds=context.window;identity.process_start_identity_us=1234;
    identity.opaque_tab_id=std::move(tab);identity.navigation_digest=std::string(64,'c');
    return identity;
  }
};

static void CheckPersistentContext() {
  using namespace seethis::core;
  VisibilityFixture f;
  f.jobs[f.a].context.native_window={1,1234,"persistent-native",1};f.Window(f.a);
  f.Open(f.a);f.Type(@"A original 世界");
  const auto generation=f.editor.presentationGeneration;
  const auto selected=NSMakeRange(2,3);[f.editor.textView setSelectedRange:selected];
  STMarkNotePanel* inspector=[[STMarkNotePanel alloc] initWithContentRect:NSMakeRect(100,100,100,100)
      styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskNonactivatingPanel backing:NSBackingStoreBuffered defer:NO];
  inspector.releasedWhenClosed=NO;[inspector makeKeyAndOrderFront:nil];[inspector makeKeyWindow];
  Check(NSApp.keyWindow==inspector,"context fixture establishes receiving key panel before focus preservation checks");
  [f.visibility applicationActivated:NSProcessInfo.processInfo.processIdentifier];
  [f.visibility applicationActivated:20];
  for(int kind=0;kind<8;++kind) {
    auto changed=f.Observation(f.a);
    if(kind==0)changed.window_id++;
    if(kind==1)changed.window_title="Other title";
    if(kind==2)changed.bounds.width++;
    if(kind==3)changed.title.availability=TitleAvailability::kAmbiguous;
    if(kind==4)changed.availability=WindowAvailability::kUnavailable;
    if(kind==5)changed.observed.monotonic_us-=3'000'000;
    if(kind==6)changed.native_window.process_start_identity_us++;
    if(kind==7)changed.native_window.incarnation++;
    [f.visibility publishWindow:changed];
    Check(f.editor.presentationOpen && f.editor.referenceID==f.a && f.editor.markID==f.Mark(f.a) &&
          f.editor.presentationGeneration==generation && f.writes.empty() && f.copies.empty() &&
          NSEqualRanges(f.editor.textView.selectedRange,selected) && NSApp.keyWindow==inspector,
          "app/window/title/freshness/native lifetime changes preserve immutable editor without focus steal, reload, save or copy");
    Check(!ReferenceVisible(f.jobs[f.a].context,f.jobs[f.a].page_identity,
          f.marks->window_observation(),f.marks->page_observation()),
          "persistent editor never grants source ReferenceVisible to mismatched observation");
    f.Window(f.a);
  }
  f.Window(f.b);f.Open(f.b);
  Check(f.editor.referenceID==f.b && f.editor.markID==f.Mark(f.b) && f.writes.size()==1 &&
        f.writes.back().key==VisibilityFixture::Key(f.a,f.Mark(f.a)) && f.copies.empty(),
        "equal ordinal across references saves exact A before B with no copy");
  f.Window(f.a);Check(f.editor.referenceID==f.b,"frontmost/Inspector context is independent of editor owner");
  f.Open(f.a);Check(f.editor.referenceID==f.a && [f.editor.textView.string isEqualToString:@"A original 世界"] && f.copies.empty(),
        "explicit A switch restores A bytes and no clipboard action");
  f.Open(f.a);Check(!f.editor.presentationOpen && f.copies==std::vector<std::string>{f.a},
        "actual visibility coordinator same exact pair uses typed original-ID completion");
  [inspector close];
}
static void CheckChromeTransitions() {
  using namespace seethis::core;
  VisibilityFixture f;
  for(const auto& id:{f.a,f.b}) {
    f.jobs[id].context.bundle_id="com.google.Chrome";
    f.jobs[id].context.pid=f.jobs[id].context.window_pid=30;
    f.jobs[id].context.window_id=303;f.jobs[id].page_identity=f.Page(id,id==f.a?"tab-A":"tab-B");
  }
  f.Window(f.a);seethis::platform::ChromeObservationLifecycle lifecycle;
  const auto publish=[&](const std::string& id) {
    auto request=lifecycle.BeginRequest();const auto foreground=f.marks->window_observation();
    return [f.visibility acceptPage:{PageAvailability::kReady,request.generation,Now(),f.jobs[id].page_identity}
        request:request lifecycle:lifecycle foreground:foreground permission:1 currentPermission:1];
  };
  Check(publish(f.a),"fresh original Chrome page accepted");f.Open(f.a);f.Type(@"Original page A");
  const auto generation=f.editor.presentationGeneration;
  const auto delayed=lifecycle.BeginRequest();const auto foreground=f.marks->window_observation();
  (void)lifecycle.Invalidate();Check(publish(f.b),"fresh page B accepted");
  Check(f.editor.presentationOpen && f.editor.referenceID==f.a && f.writes.empty() && f.copies.empty() &&
        !ReferenceVisible(f.jobs[f.a].context,f.jobs[f.a].page_identity,
            f.marks->window_observation(),f.marks->page_observation()),"page B hides A source eligibility while A editor persists");
  Check(![f.visibility acceptPage:{PageAvailability::kReady,delayed.generation,Now(),f.jobs[f.a].page_identity}
      request:delayed lifecycle:lifecycle foreground:foreground permission:1 currentPermission:1],"stale page operation is rejected");
  for(auto availability:{PageAvailability::kUnavailable,PageAvailability::kAmbiguous,PageAvailability::kTimedOut}) {
    auto request=lifecycle.BeginRequest();
    Check([f.visibility acceptPage:{availability,request.generation,Now(),{}}
      request:request lifecycle:lifecycle foreground:foreground permission:1 currentPermission:1],"truthful unavailable page publication accepted");
    Check(f.editor.presentationOpen && f.editor.presentationGeneration==generation && f.writes.empty() && f.copies.empty(),
          "page failure/cancel preserves valid original draft and never copies");
  }
  auto request=lifecycle.BeginRequest();
  Check(![f.visibility acceptPage:{PageAvailability::kUnavailable,request.generation,Now(),{}}
    request:request lifecycle:lifecycle foreground:foreground permission:1 currentPermission:2],"permission generation rejects late provider callback");
  (void)lifecycle.Invalidate();Check(publish(f.b),"page B current for explicit switch");f.Open(f.b);
  Check(f.editor.referenceID==f.b && f.writes.size()==1 && f.writes[0].key.first==f.a && f.copies.empty(),
        "explicit equal-number page B saves original page A then switches without copy");
}
static void CheckRetirementAndDrafts() {
  using namespace seethis::core;
  for(const auto status:{STNoteSaveStatus::kFailed,STNoteSaveStatus::kUnavailable,
                         STNoteSaveStatus::kConflict,STNoteSaveStatus::kCommitUncertain}) {
    VisibilityFixture f;f.saveStatus=status;f.Open(f.a);f.Type(@"Retained A 世界");
    NSTimer* timer=f.editor.saveTimer;const auto generation=f.editor.presentationGeneration;
    Check(f.settings->SetGlobalVisibility(false),"global Hide persists");[f.visibility reconcile];
    Check(!f.editor.presentationOpen && !timer.valid && f.writes.size()==1 && f.copies.empty(),
          "global Hide retires without copy and attempts valid exact draft save once");
    Check(f.settings->SetGlobalVisibility(true),"global Show persists");f.Window(f.a);[f.visibility reconcile];
    Check(!f.editor.presentationOpen,"Show and context return never auto-open failed draft");
    f.Open(f.a);Check([f.editor.textView.string isEqualToString:@"Retained A 世界"] && !f.editor.retryButton.hidden,
          "explicit valid owner reopen restores exact failed draft honestly");
    const auto before=f.writes.size();[f.editor autosaveTimer:timer generation:generation];
    Check(f.writes.size()==before && f.copies.empty(),"retired timer cannot save or complete reopened same pair");
    f.Window(f.b);f.Open(f.b);Check(f.editor.referenceID==f.a && f.copies.empty(),"failed old save blocks even equal-ordinal reference switch");
    f.saveStatus=STNoteSaveStatus::kUpdated;f.Open(f.b);
    Check(f.editor.referenceID==f.b && f.writes.back().key.first==f.a && f.copies.empty(),"explicit switch succeeds after old durable save without copy");
  }
  for(int loss=0;loss<8;++loss) {
    VisibilityFixture f;f.Open(f.a);f.Type(@"Lifecycle A");
    NSTimer* timer=f.editor.saveTimer;
    if(loss==0)f.jobs.erase(f.a);
    if(loss==1)f.jobs[f.a].state=ReferenceJobState::kDeleted;
    if(loss==2)f.jobs[f.a].state=ReferenceJobState::kExpired;
    if(loss==3)f.snapshots[f.a].marks.erase(f.snapshots[f.a].marks.begin());
    if(loss==4)f.permission=false;
    if(loss==5)f.displays[0].scale=2;
    if(loss==6)f.snapshots[f.a].status=MarkNotesStatus::kCorrupt;
    if(loss==7)f.snapshots[f.a].marks[0].anchor={};
    [f.visibility reconcile];f.Open(f.a);[timer fire];
    Check(!f.editor.presentationOpen && !timer.valid && f.copies.empty(),
          "deleted/expired/missing-mark/safety/topology/metadata/anchor loss retires without copy or resurrection");
    if(loss<4)Check(f.writes.empty(),"terminal owner never saves or recreates dead bytes");
  }
}

#if defined(SEETHIS_NCV_FULL_COORDINATOR_TESTING)
// External header adapters and action receipts only; production views own all
// geometry, ancestry, layout and native hit testing. No OS service is invoked.
@interface STVisibilityLayoutAdapter : NSObject <STInspectorFeedbackAdapter,STInspectorChromeAdapter>
@property(nonatomic) seethis::platform::ChromeConnectionSnapshot state;
@property(nonatomic) NSUInteger visibilityActions;
@property(nonatomic) NSUInteger deleteActions;
@end
@implementation STVisibilityLayoutAdapter
- (BOOL)hasMailHandlerForURL:(NSURL*)url {(void)url;return NO;}
- (BOOL)openMailURL:(NSURL*)url {(void)url;return NO;}
- (BOOL)copyEmailAddress:(NSString*)address {(void)address;return NO;}
- (seethis::platform::ChromeConnectionSnapshot)connection {return self.state;}
- (seethis::platform::EffectiveShortcutSnapshot)shortcuts {return {};}
- (std::int64_t)foregroundChromePID {return 51;}
- (void)connect {}
- (void)retry {}
- (BOOL)openSettings {return NO;}
- (void)inspectorToggleMarkVisibility:(id)sender {(void)sender;++_visibilityActions;}
- (void)inspectorDeleteAll:(id)sender {(void)sender;++_deleteActions;}
@end
static NSUInteger CountAction(NSView* view,SEL action) {
  NSUInteger count=[view isKindOfClass:NSButton.class] && [(NSButton*)view action]==action;
  for(NSView* child in view.subviews)count+=CountAction(child,action);
  return count;
}
static void CheckFixedActions() {
  using namespace seethis::platform;
  STVisibilityLayoutAdapter* adapter=[STVisibilityLayoutAdapter new];
  STInspectorContainerView* container=[[STInspectorContainerView alloc]
      initWithFrame:NSMakeRect(0,0,470,430)];
  NSView* host=[[NSView alloc] initWithFrame:container.frame];
  [host addSubview:container];
  STInspectorView* document=[[STInspectorView alloc] initWithFrame:NSZeroRect];
  STInspectorChromeView* chrome=[[STInspectorChromeView alloc] initWithAdapter:adapter];
  container.chromeView=chrome;
  container.feedbackView=[[STInspectorFeedbackView alloc] initWithAdapter:adapter];
  container.referenceSelector=[[STInspectorReferenceSelector alloc] initWithFrame:NSZeroRect];
  container.referenceScrollView=[NSScrollView new];
  container.referenceScrollView.hasVerticalScroller=YES;
  container.referenceScrollView.documentView=document;
  container.markVisibilityButton=document.markVisibilityButton;
  container.deleteAllButton=document.deleteAllButton;
  for(NSView* child in @[container.feedbackView,chrome,container.referenceSelector,
      container.referenceScrollView,document.markVisibilityButton,document.deleteAllButton])
    [container addSubview:child];
  document.markVisibilityButton.target=adapter;document.deleteAllButton.target=adapter;
  document.text=[@"Long synthetic metadata 世界\n" stringByPaddingToLength:4000
      withString:@"Long synthetic metadata 世界\n" startingAtIndex:0];
  document.markRows=@[@"1  Long saved note 世界",@"2  Another saved note"];
  for(int pass=0;pass<3;++pass)for(NSValue* size in @[
      [NSValue valueWithSize:NSMakeSize(470,430)],
      [NSValue valueWithSize:NSMakeSize(720,760)]])
    for(NSNumber* recovery in @[@NO,@YES])for(NSNumber* selected in @[@NO,@YES])
      for(NSString* state in @[@"visible",@"invisible",@"uncertain",@"unavailable"]) {
        container.frame=NSMakeRect(0,0,size.sizeValue.width,size.sizeValue.height);
        ChromeConnectionSnapshot connection;connection.target={51,123};
        connection.permission.consent=recovery.boolValue?ChromeConsent::kDenied:ChromeConsent::kGranted;
        connection.provider=ChromeProviderState::kReady;adapter.state=connection;
        chrome.chromeContextActive=YES;[chrome refreshChrome];
        container.feedbackView.feedback.copyAddressAvailable=recovery.boolValue;
        container.feedbackView.feedback.status=recovery.boolValue?
            @"Could not open your email app. Copy the email address to send feedback.":@"";
        document.selectedReferenceID=selected.boolValue?@"synthetic-reference":nil;
        document.globalVisibilityName=state;
        document.globalReferenceVisible=[state isEqualToString:@"visible"];
        document.markVisibilityAvailable=![state isEqualToString:@"unavailable"];
        document.deleteAllAvailable=YES;document.deleteAllRetry=recovery.boolValue;
        [document fitToWidth:size.sizeValue.width];[container layout];
        const NSRect visibility=document.markVisibilityButton.frame, deletion=document.deleteAllButton.frame;
        Check(document.markVisibilityButton.superview==container && document.deleteAllButton.superview==container &&
              CountAction(container,@selector(inspectorToggleMarkVisibility:))==1 &&
              CountAction(container,@selector(inspectorDeleteAll:))==1,
              "exactly one of each production action is owned outside the scroll document");
        Check(NSContainsRect(container.bounds,visibility) && NSContainsRect(container.bounds,deletion) &&
              NSMinY(visibility)==NSMinY(deletion) && NSMaxX(visibility)+8<=NSMinX(deletion) &&
              !NSIntersectsRect(visibility,deletion) &&
              NSMaxY(visibility)<NSMinY(container.referenceScrollView.frame) &&
              NSHeight(container.referenceScrollView.frame)>0 &&
              NSMinY(visibility)>=NSMaxY(chrome.frame) &&
              NSMinY(visibility)>=NSMaxY(container.referenceSelector.frame),
              "minimum/normal fixed row fits below dynamic headers with separate eight point hit gap above usable viewport");
        for(NSButton* button in @[document.markVisibilityButton,document.deleteAllButton]) {
          Check(NSWidth(button.frame)>=button.fittingSize.width && NSHeight(button.frame)>=button.fittingSize.height,
                "confirmed and simultaneous retry titles fit their native controls");
          for(NSNumber* fraction in @[@0.01,@0.5,@0.99]) {
            const NSPoint point=NSMakePoint(NSMinX(button.frame)+NSWidth(button.frame)*fraction.doubleValue,NSMidY(button.frame));
            NSView* hit=[container hitTest:[container convertPoint:point toView:host]];
            Check(hit==button,"production native center and facing edges hit only the intended button");
            const auto beforeVisibility=adapter.visibilityActions,beforeDelete=adapter.deleteActions;
            [(NSButton*)hit performClick:nil];
            Check(adapter.visibilityActions==beforeVisibility+(button==document.markVisibilityButton && button.enabled) &&
                  adapter.deleteActions==beforeDelete+(button==document.deleteAllButton && button.enabled),
                  "hit-dispatched native action affects exactly its own receipt and disabled action has no effect");
          }
        }
        const NSPoint gap=NSMakePoint((NSMaxX(visibility)+NSMinX(deletion))/2,NSMidY(visibility));
        Check([container hitTest:[container convertPoint:gap toView:host]]==container,"native action gap routes to background only");
        [container.referenceScrollView.contentView scrollToPoint:NSMakePoint(0,NSHeight(document.bounds)-100)];
        [container.referenceScrollView reflectScrolledClipView:container.referenceScrollView.contentView];
        [document fitToWidth:NSWidth(document.bounds)];
        Check(NSEqualRects(visibility,document.markVisibilityButton.frame) && NSEqualRects(deletion,document.deleteAllButton.frame),
              "scrolling long notes and body refresh cannot move or overwrite fixed action frames");
        const auto body=InspectorBodyLayout(NSWidth(document.bounds),0,0,document.markRows,selected.boolValue);
        Check(body.textTop-body.controlsTop==(selected.boolValue?92:4),
              "body contains only remaining selected actions without obsolete visibility row reservation");
        NSString* expected=[state isEqualToString:@"visible"]?@"Hide Marks":
            [state isEqualToString:@"invisible"]?@"Show Marks":@"Retry visibility save";
        Check([document.markVisibilityButton.title isEqualToString:expected] &&
              [document.markVisibilityButton.accessibilityLabel containsString:@"All references"],
              "state projects truthful compact action and accessible all-reference scope");
      }
}
// Run this same file with production platform objects/core/service linked. The
// designated app initializer owns all dependencies and tears down normally.
@interface STNCVCoordinatorProbe : STOverlayCoordinator
- (void)testContext:(const seethis::core::Context&)context;
- (void)clearTestSelection;
- (BOOL)hasPaint;
- (std::string)hoveredReference;
- (std::vector<STNoteBadge>)badges;
- (void)useFixtureEnvironment;
- (void)testOrdinaryCancellation;
- (void)installSyntheticReferenceCopy:(std::vector<std::string>*)copies;
@end
@implementation STNCVCoordinatorProbe
- (void)installSyntheticReferenceCopy:(std::vector<std::string>*)copies {
  // Bind both incidental selection and explicit JSON copy to synthetic receipts
  // before any action; no production clipboard callback can execute here.
  _marks=std::make_unique<seethis::core::MarkController>(*_references,
      [copies](const seethis::core::StoredReference& stored) {
        copies->push_back(stored.value.id);return true;
      });
}
- (void)testOrdinaryCancellation {
  // A real controller Cancel followed by the production drawing->idle refresh,
  // without starting a capture or querying the user's screen.
  const seethis::core::DisplayPoint point{1,seethis::core::CoordinateUnit::kLogicalPoints,{20,20},1};
  Check(self.controller->ShortcutKeyDown(100,point)==seethis::core::StartResult::kStarted,
        "ordinary cancellation fixture starts a real controller drawing transaction");
  _previousState=seethis::core::InteractionState::kDrawing;
  Check(self.controller->Cancel(seethis::core::CancelReason::kUser),
        "ordinary cancellation fixture accepts real controller user cancellation");
  [self refresh];
}
- (void)rebuildDisplays {
  // An unshown native panel observes real pointer acceptance without creating a
  // screen overlay or moving the user's pointer.
  STOverlayPanel* panel=[[STOverlayPanel alloc]
      initWithContentRect:NSMakeRect(0,0,100,100)
      styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
  panel.displayID=1;panel.ignoresMouseEvents=YES;
  panel.contentView=[[NSView alloc] initWithFrame:NSMakeRect(0,0,100,100)];
  [self.panels addObject:panel];
}
- (void)useFixtureEnvironment {
  // Replace environmental observations only; native editor persistence,
  // eligibility, badge caching, hit testing, painting and hover remain real.
  STNoteVisibilitySource source;
  source.marks=_marks.get();
  source.visibility=[settings=_settings]{return settings->Visibility();};
  source.lookup=[references=_references](std::string_view id){return references->LookupMetadata(id);};
  source.snapshots=[references=_references](std::string_view id){return references->MarkSnapshots(id);};
  __weak STNCVCoordinatorProbe* weakSelf=self;
  source.displays=[weakSelf]{
    STNCVCoordinatorProbe* owner=weakSelf;
    return owner?owner->_displays:std::vector<seethis::core::FrozenDisplay>{};
  };
  source.foregroundPID=[weakSelf]{
    STNCVCoordinatorProbe* owner=weakSelf;
    return owner?owner->_marks->window_observation().pid:std::int64_t{0};
  };
  source.screenReady=[]{return true;};
  _noteVisibility=[[STNoteVisibilityCoordinator alloc] initWithEditor:_noteEditor source:std::move(source)];
}
- (void)testContext:(const seethis::core::Context&)context {
  _displays=context.displays;
  const auto previous=_marks->window_observation();
  [_noteVisibility publishWindow:{seethis::core::WindowAvailability::kReady,
      _marks->window_observation().generation+1,seethis::core::Now(),context.window_pid,
      context.bundle_id,context.window_id,context.window,context.native_window,
      context.window_title,context.title.version?context.title:
          seethis::core::TitleEvidence{1,seethis::core::TitleAvailability::kReady}}];
  // This is the same publication/invalidation order as refreshForegroundWindow.
  // In particular, preserve the stationary _lastPointer across this boundary.
  if(!STSameWindowIdentity(previous,_marks->window_observation()))
    [self invalidateVisibilityPresentation];
}
- (std::string)hoveredReference {return _hoveredID;}
- (std::vector<STNoteBadge>)badges {[self refreshNoteBadges];return _noteBadges;}
- (STDeleteChord)physicalDeleteChord {return {};}
- (void)clearTestSelection {
  _inspectorLifecycle.selected_id.clear();_selectedMarkID.clear();[self refreshInspector];
}
- (BOOL)hasPaint {
  NSBitmapImageRep* bitmap=[[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nil
      pixelsWide:100 pixelsHigh:100 bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES
      isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:400 bitsPerPixel:32];
  std::fill(bitmap.bitmapData,bitmap.bitmapData+40000,0);
  [NSGraphicsContext saveGraphicsState];
  NSGraphicsContext.currentContext=[NSGraphicsContext graphicsContextWithBitmapImageRep:bitmap];
  [self paintDisplay:1];[NSGraphicsContext restoreGraphicsState];
  return std::any_of(bitmap.bitmapData,bitmap.bitmapData+40000,[](auto byte){return byte!=0;});
}
- (void)refreshForegroundWindow {}  // Deterministic unavailable source fixture.
@end
static void CheckProductionInvalidationWiring(bool sameNative=false) {
  using namespace seethis::core;
  char path[]="/tmp/seethis-ncv-coordinator-XXXXXX";
  const std::filesystem::path root=mkdtemp(path);
  {
    ReferenceStore store(root/"references");
    bool failSync=false;
    SettingsStore settings(root/"settings.json",[&](SettingsPersistPoint p){
      return !failSync || p!=SettingsPersistPoint::kDirectorySync;
    });
    Context context;context.pid=context.window_pid=10;context.self_pid=99;
    context.bundle_id="app.A";context.window_title="Synthetic A";context.app_name="Fixture";context.executable="/fixture";
    context.window_id=101;context.window={0,0,100,100};context.quartz_to_appkit_top=100;
    context.observed=Now();context.selection_method="frontmost-pid/topmost-normal-containing-initial-point";
    context.exclusion_method="exact-window-allowlist;self-pid-rejected;child-windows-off";
    if(sameNative) {context.title={1,TitleAvailability::kReady};context.native_window={1,1234,"production-title",1};}
    context.excluded_window_ids={102};
    context.displays={{"display",1,{0,0,100,100},{0,0,100,100},1,{1,-1,0,100}}};
    const std::string a(32,'a'),b(32,'b');
    for(const auto& id:{a,b}) {
      MarkController creator(store,[](const StoredReference&){return true;});
      const auto generation=creator.Begin(id);
      auto sourceContext=context;
      if(id==b) {
        if(sameNative)sourceContext.window_title="Synthetic B";
        else sourceContext.window_id=202;
      }
      Check(creator.BindContext(generation,sourceContext),"full coordinator source context binds");
      CaptureResult capture;capture.requested=Now();capture.completed=Now();
      capture.pixels={100,100,Bytes(40000,255)};creator.Captured(generation,std::move(capture));
      const auto point=[](double x,double y){return DisplayPoint{1,CoordinateUnit::kLogicalPoints,{x,y},1};};
      creator.Release(generation,std::vector<std::vector<DisplayPoint>>{
          {point(20,20),point(40,40)},
          {point(70,60),point(80,70),point(70,80),point(60,70),point(70,60)}},Now());
      store.WaitForIdleForTesting();
      Check(creator.phase()==ReferencePhase::kCommitted,"full coordinator real ready reference persists");
      Check(store.UpdateMarkVisibility(id,StableMarkId(id,0),false,0).status==MarkNoteUpdateStatus::kUpdated,
          "full coordinator dormant old bit is hidden");
    }
    seethis::service::ReferenceServer server(store,settings,root/"web",nullptr);
    Check(server.Start(),"fixture-owned loopback server supplies actual explicit-copy readiness");
    InteractionController interaction;DeleteGestureController deletion;
    @autoreleasepool {
      STNCVCoordinatorProbe* coordinator=[[STNCVCoordinatorProbe alloc]
          initWithController:&interaction references:&store settings:&settings server:&server deletion:&deletion];
      std::vector<std::string> referenceCopies;
      [coordinator installSyntheticReferenceCopy:&referenceCopies];
      [coordinator useFixtureEnvironment];
      STMarkNoteEditor* editor=[coordinator valueForKey:@"noteEditor"];
      std::vector<std::string> copiedReferences;
      [editor setCopyCallback:[&](std::string_view id)->STNoteCopyStatus {
        copiedReferences.emplace_back(id);return STNoteCopyStatus::kCopied;
      }];
      [coordinator testContext:context];[coordinator clearTestSelection];
      STInspectorView* view=[coordinator valueForKey:@"inspectorView"];
      NSPanel* actionPanel=[coordinator valueForKey:@"inspectorPanel"];
      STInspectorContainerView* actionContainer=(STInspectorContainerView*)actionPanel.contentView;
      [actionContainer layout];
      Check(view.markVisibilityButton.superview==actionContainer && view.deleteAllButton.superview==actionContainer &&
            actionContainer.markVisibilityButton==view.markVisibilityButton && actionContainer.deleteAllButton==view.deleteAllButton &&
            view.markVisibilityButton.target==coordinator && view.deleteAllButton.target==coordinator &&
            CountAction(actionContainer,@selector(inspectorToggleMarkVisibility:))==1 &&
            CountAction(actionContainer,@selector(inspectorDeleteAll:))==1,
            "actual coordinator installs the sole existing buttons in fixed container with original action targets");
      const DisplayPoint hit{1,CoordinateUnit::kLogicalPoints,{20,20},1};
      Check(!view.markVisibilityButton.hidden&&view.markVisibilityButton.enabled&&
            [view.markVisibilityButton.title isEqualToString:@"Hide Marks"],
            "actual Inspector global switch is present and enabled with no selection");
      Check([coordinator visibleHitIdentity:hit radius:2].has_value()&&
            [coordinator noteBadgeAtPoint:hit].has_value()&&[coordinator hasPaint],
            "actual coordinator renders ink/badges and accepts hits despite dormant old hidden bits");
      [view.markVisibilityButton performClick:nil];
      Check(settings.Visibility().state==GlobalVisibilityState::kInvisible&&
            ![coordinator visibleHitIdentity:hit radius:2]&&! [coordinator noteBadgeAtPoint:hit]&&
            ![coordinator hasPaint],
            "actual no-selection global switch suppresses all reference ink/badges/hits");
      Check(SettingsStore(root/"settings.json").Visibility().state==GlobalVisibilityState::kInvisible &&
            [view.markVisibilityButton.title isEqualToString:@"Show Marks"] && actionContainer.needsLayout,
            "relocated Hide persists across settings reconstruction and refresh schedules fixed row layout");
      [actionContainer layout];
      [view.markVisibilityButton performClick:nil];
      Check(settings.Visibility().permits_presentation()&&[coordinator hasPaint],
            "actual global switch restores display without per-mark rewrite");
      Check(SettingsStore(root/"settings.json").Visibility().state==GlobalVisibilityState::kVisible &&
            [view.markVisibilityButton.title isEqualToString:@"Hide Marks"],
            "relocated Show persists across reconstruction with correct action label");

      // Warm the cache and hover over only the badge: this point is outside
      // the ink's hit radius, so stale ink cannot explain pointer interception.
      const DisplayPoint stationary{1,CoordinateUnit::kLogicalPoints,{8,20},1};
      const auto initialBadges=[coordinator badges];
      Check(initialBadges.size()==2&&initialBadges[0].reference_id==a&&
            initialBadges[0].mark_id==StableMarkId(a,0)&&initialBadges[0].number==1&&
            initialBadges[1].mark_id==StableMarkId(a,1)&&initialBadges[1].number==2,
            "current window projects both line and circle with stable IDs and numbers");
      Check(![coordinator visibleHitIdentity:stationary radius:10]&&
            [coordinator noteBadgeAtPoint:stationary].has_value(),
            "stationary fixture is a genuine badge-only hit");
      [coordinator updatePointerPolicy:stationary];
      STOverlayPanel* panel=coordinator.panels.firstObject;
      Check(!panel.ignoresMouseEvents&&[coordinator hoveredReference]==a,
            "real pointer policy accepts the warmed current badge and sets hover");
      Check([coordinator handleNoteBadgeClick:stationary]&&editor.presentationOpen&&
            editor.referenceID==a&&editor.markID==StableMarkId(a,0),
            "actual numbered click explicitly opens the current stable note pair");
      editor.textView.string=@"Selection keeps original A draft";
      [editor textDidChange:[NSNotification notificationWithName:NSTextDidChangeNotification object:editor.textView]];
      const auto selectionGeneration=editor.presentationGeneration;
      auto selectionB=context;
      if(sameNative)selectionB.window_title="Synthetic B";else selectionB.window_id=202;
      for(const auto& id:{a,b}) {
        [coordinator testContext:id==a?context:selectionB];
        const auto before=referenceCopies.size(),noteBefore=copiedReferences.size();
        [coordinator inspectorSelectReference:[NSString stringWithUTF8String:id.c_str()]];
        Check(referenceCopies.size()==before && copiedReferences.size()==noteBefore &&
              editor.presentationOpen && editor.referenceID==a && editor.markID==StableMarkId(a,0) &&
              editor.presentationGeneration==selectionGeneration &&
              [editor.textView.string isEqualToString:@"Selection keeps original A draft"],
              "open A editor selecting visible A/B never publishes incidental clipboard or changes original draft/session");
      }
      const auto explicitBefore=referenceCopies.size();
      [coordinator inspectorCopyReference:nil];
      Check(referenceCopies.size()==explicitBefore+1 && referenceCopies.back()==b && editor.presentationOpen && editor.referenceID==a,
            "separate explicit Copy JSON remains available for selected B while A editor is open");
      [editor hide];
      for(const auto& id:{a,b}) {
        [coordinator testContext:id==a?context:selectionB];
        const auto before=referenceCopies.size();
        [coordinator inspectorSelectReference:[NSString stringWithUTF8String:id.c_str()]];
        Check(!editor.presentationOpen && referenceCopies.size()==before+1 && referenceCopies.back()==id,
              "closed-editor Inspector selection retains existing selected-reference recopy behavior");
      }
      [coordinator testContext:context];[coordinator clearTestSelection];
      Check([coordinator handleNoteBadgeClick:stationary] && editor.presentationOpen && editor.referenceID==a,
            "explicit badge restores original A after selection copy regression");
      NSPanel* inspector=[coordinator valueForKey:@"inspectorPanel"];
      NSEvent* inspectorDown=[NSEvent mouseEventWithType:NSEventTypeLeftMouseDown
          location:NSMakePoint(10,10) modifierFlags:0 timestamp:0
          windowNumber:inspector.windowNumber context:nil eventNumber:4 clickCount:1 pressure:1];
      const auto openGeneration=editor.presentationGeneration;
      Check([editor observeLocalMouseDown:inspectorDown generation:openGeneration]==inspectorDown,
            "actual Inspector outside click remains available to its original controls");
      const DisplayPoint secondNumber{1,CoordinateUnit::kLogicalPoints,{70,60},1};
      Check([coordinator handleNoteBadgeClick:secondNumber]&&editor.presentationOpen&&
            editor.markID==StableMarkId(a,1)&&editor.presentationGeneration==openGeneration+1,
            "actual different-number route switches to exact second mark once");
      [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
      Check(editor.presentationOpen&&editor.markID==StableMarkId(a,1),
            "actual new badge selection survives old deferred Inspector dismissal");
      [editor observeLocalMouseDown:inspectorDown generation:editor.presentationGeneration];
      Check([coordinator handleNoteBadgeClick:secondNumber]&&!editor.presentationOpen,
            "actual same-number route toggles closed before deferred outside callback");
      [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
      Check(!editor.presentationOpen,"actual same-number note stays closed after queued dismissal");
      [coordinator handleNoteBadgeClick:stationary];
      [editor observeLocalMouseDown:inspectorDown generation:editor.presentationGeneration];
      [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
      Check(editor.presentationOpen&&[coordinator noteBadgeAtPoint:stationary].has_value(),
            "Inspector outside click preserves editor and truthful source badge");
      editor.textView.string=@"Coordinator A draft 世界";
      [editor textDidChange:[NSNotification notificationWithName:NSTextDidChangeNotification object:editor.textView]];
      NSTimer* retiredTimer=editor.saveTimer;
      const auto retiredGeneration=editor.presentationGeneration;
      auto absent=context;
      if(sameNative)absent.window_title="Synthetic unmatched title";else absent.window_id=303;
      [coordinator testContext:absent];
      Check(![coordinator noteBadgeAtPoint:stationary]&&![coordinator hasPaint]&&
            [coordinator badges].empty()&&panel.ignoresMouseEvents&&
            [coordinator hoveredReference].empty(),
            "warmed cache invalidation removes badge paint/hit/hover for a same-app window with no marks");
      Check(editor.presentationOpen&&retiredTimer.valid&&
            ![coordinator handleNoteBadgeClick:stationary],
            "context loss preserves editor and stale badge click passes through");
      [coordinator testContext:context];
      Check(editor.presentationOpen&&!panel.ignoresMouseEvents&&[coordinator hoveredReference]==a&&
            [editor.textView.string isEqualToString:@"Coordinator A draft 世界"],
            "window return restores badge eligibility while preserving exact A editor");
      [editor autosaveTimer:retiredTimer generation:retiredGeneration];
      Check(editor.presentationOpen&&editor.referenceID==a,
            "retired pre-transition callback cannot retire the explicit replacement editor");
      auto windowB=context;
      if(sameNative)windowB.window_title="Synthetic B";else windowB.window_id=202;
      for(int pass=0;pass<4;++pass) {
        [coordinator testContext:windowB];
        const auto badgeB=[coordinator noteBadgeAtPoint:stationary];
        Check(badgeB&&badgeB->reference_id==b&&badgeB->mark_id==StableMarkId(b,0)&&
              badgeB->number==1&&editor.presentationOpen&&editor.referenceID==a&&[coordinator hoveredReference]==b,
              "rapid same-PID window B publishes only B badge/hover and preserves original A editor");
        [coordinator testContext:context];
        const auto badgeA=[coordinator noteBadgeAtPoint:stationary];
        Check(badgeA&&badgeA->reference_id==a&&badgeA->mark_id==StableMarkId(a,0)&&
              badgeA->number==1&&editor.presentationOpen&&editor.referenceID==a&&[coordinator hoveredReference]==a,
              "rapid same-PID window A return preserves stable badge identity without editor rebind");
      }
      const auto beforeStop=copiedReferences.size();
      [coordinator stopChromePageRefresh];[coordinator abortPending:@"Escape"];
      [coordinator testOrdinaryCancellation];
      Check(editor.presentationOpen&&editor.referenceID==a&&copiedReferences.size()==beforeStop,
            "actual provider stop and ordinary Escape/capture drawing-idle cancellation preserve original editor without copy");
      editor.textView.string=@"Dirty fixed-row Hide draft 世界";
      [editor textDidChange:[NSNotification notificationWithName:NSTextDidChangeNotification object:editor.textView]];
      NSTimer* hideTimer=editor.saveTimer;const auto hideGeneration=editor.presentationGeneration;
      const auto hideCopies=copiedReferences.size(),hideReferenceCopies=referenceCopies.size();
      [view.markVisibilityButton performClick:nil];
      [editor autosaveTimer:hideTimer generation:hideGeneration];
      Check(!hideTimer.valid && copiedReferences.size()==hideCopies && referenceCopies.size()==hideReferenceCopies &&
            store.MarkSnapshots(a).marks[0].note=="Dirty fixed-row Hide draft 世界",
            "relocated Hide saves dirty original owner, invalidates stale timer and never invokes either injected copy path");
      [coordinator testContext:windowB];[coordinator testContext:context];
      Check(![coordinator noteBadgeAtPoint:stationary]&&![coordinator hasPaint]&&
            panel.ignoresMouseEvents&&[coordinator hoveredReference].empty()&&!editor.presentationOpen,
            "global Invisible dominates cached A/B/A badge, paint, stationary hover and notes");
      [view.markVisibilityButton performClick:nil];
      Check([coordinator noteBadgeAtPoint:stationary]&&[coordinator hasPaint]&&
            !panel.ignoresMouseEvents&&!editor.presentationOpen,
            "global Visible restores the same current marks without reopening a dismissed note");
      auto other=context;other.window_id++;
      [coordinator testContext:other];
      Check(![coordinator visibleHitIdentity:hit radius:2]&&![coordinator noteBadgeAtPoint:hit]&&
            ![coordinator hasPaint],"global Visible cannot bypass actual coordinator source-window gate");
      [coordinator testContext:context];failSync=true;
      [view.markVisibilityButton performClick:nil];
      Check(settings.Visibility().state==GlobalVisibilityState::kUncertain&&
            ![coordinator visibleHitIdentity:hit radius:2]&&![coordinator noteBadgeAtPoint:hit]&&
            ![coordinator hasPaint]&&[view.markVisibilityButton.title containsString:@"Retry"],
            "actual uncertain save shows retry and suppresses every presentation path");
      failSync=false;[coordinator inspectorToggleMarkVisibility:nil];
      Check(settings.Visibility().state==GlobalVisibilityState::kInvisible,
            "uncertain global control retries requested Invisible durably without flipping it");
      [view.markVisibilityButton performClick:nil];
      Check(settings.Visibility().permits_presentation()&&[coordinator hasPaint],
            "actual coordinator recovers only after confirmed explicit Visible");
      [editor toggleReference:std::string(32,'a') mark:"orphan" number:1 screenPoint:NSMakePoint(300,300)];
      Check(editor.panel.isVisible,"production fixture explicitly opens native editor");
      [coordinator invalidateVisibilityPresentation];
      Check(!editor.panel.isVisible,"actual coordinator invalidation invokes production note reconciliation");
      [coordinator publishForegroundUnavailable];
      [coordinator stopChromePageRefresh];
      Check(!editor.panel.isVisible,"production unavailable/stop paths remain idempotent and closed");
      coordinator=nil;
    }
  }
  std::filesystem::remove_all(root);
}
#endif

static void CheckP4OwningShotTopology() {
  using namespace seethis::core;
  char pattern[]="/tmp/seethis-p4-note-owner-XXXXXX";const std::filesystem::path directory=mkdtemp(pattern);
  {
    ReferenceStore store(directory);MarkController marks(store,[](const auto&){return true;});
    const std::string root(32,'a'),second(32,'b'),third(32,'c');
    FrozenDisplay first;first.uuid="original";first.id=1;first.scale=1;
    first.logical=first.pixels={0,0,100,100};auto replacement=first;replacement.uuid="replacement";
    Reference old;old.id=root;old.context.displays={first};old.regions.resize(1);
    Reference actual=old;actual.first_mark_slot=1;actual.context.displays={replacement};
    ReferenceJobSnapshot job;job.id=root;job.state=ReferenceJobState::kReady;job.context=old.context;
    MarkSnapshot mark;mark.id=StableMarkId(root,1);mark.slot=1;mark.number=2;mark.shot_id=second;
    mark.anchor={1,CoordinateUnit::kLogicalPoints,{20,20},1};mark.note="original second-shot owner";
    MarkNotesSnapshot notes;notes.status=MarkNotesStatus::kReady;notes.marks={mark};
    ReferenceCollection collection;collection.status=MarkNotesStatus::kReady;
    collection.shots={{root,0,std::make_shared<const Reference>(old)},
        {second,1,std::make_shared<const Reference>(actual)}};
    auto displays=std::vector<FrozenDisplay>{replacement};GlobalVisibilitySnapshot shown;shown.state=GlobalVisibilityState::kVisible;
    STNoteVisibilitySource source;source.marks=&marks;
    source.lookup=[&](std::string_view){return std::optional<ReferenceJobSnapshot>(job);};
    source.snapshots=[&](std::string_view){return notes;};source.collection=[&](std::string_view){return collection;};
    source.displays=[&]{return displays;};source.visibility=[&]{return shown;};source.screenReady=[]{return true;};
    source.foregroundPID=[]{return std::int64_t{0};};
    STNoteVisibilityCoordinator* validator=[[STNoteVisibilityCoordinator alloc] initWithEditor:nil source:source];
    Check([validator validReference:root mark:mark.id],"note owner uses authoritative second-shot replacement topology");
    const auto preview=seethis::platform::MarksForShot(notes,root);
    Check(preview.marks.empty()&&[validator validReference:root mark:mark.id],"carousel filtering cannot retarget original note owner");
    auto newest=actual;newest.first_mark_slot=2;collection.shots.push_back({third,2,std::make_shared<const Reference>(newest)});
    marks.RetireLiveShot();
    Check([validator validReference:root mark:mark.id],"persistent owner remains valid after append and lease retirement");
    Check(![validator eligibleReference:root mark:mark.id ownFocus:NO],"retired historical owner is not a live badge entry");
    notes.marks[0].shot_id="unknown";
    Check(![validator validReference:root mark:mark.id],"unknown shot membership never borrows first-shot topology");
    notes.marks[0]=mark;displays={first};
    Check(![validator validReference:root mark:mark.id],"first topology cannot substitute for owning shot topology");
    displays={replacement};job.state=ReferenceJobState::kDeleted;
    Check(![validator validReference:root mark:mark.id],"root deletion invalidates original note owner");
  }
  std::filesystem::remove_all(directory);
}

static void CheckP4ContinuousNoteMembership() {
  using namespace seethis::core;
  char pattern[]="/tmp/seethis-p4-admitted-notes-XXXXXX";const std::filesystem::path directory=mkdtemp(pattern);
  {
    ReferenceStore store(directory);MarkController marks(store,[](const auto&){return true;});
    const std::string root(32,'a'),next(32,'b');Context context;
    context.pid=context.window_pid=42;context.self_pid=99;context.window_id=7;
    context.app_name="Fixture";context.bundle_id="test.fixture";context.executable="/fixture";
    context.window_title="Current viewport";context.window={0,0,100,100};context.quartz_to_appkit_top=100;
    context.observed=Now();context.native_window={1,700,"badge-source",1};context.selection_method="frontmost-pid/topmost-normal-containing-initial-point";
    context.excluded_window_ids={8};context.exclusion_method="exact-window-allowlist;self-pid-rejected;child-windows-off";
    context.displays.push_back({"display",1,{0,0,100,100},{0,0,100,100},1,{1,-1,0,100}});
    WindowObservation observed;observed.availability=WindowAvailability::kReady;observed.generation=1;
    observed.observed=Now();observed.pid=42;observed.bundle_id=context.bundle_id;observed.window_id=7;
    observed.native_window=context.native_window;observed.bounds=context.window;observed.window_title=context.window_title;observed.title={1,TitleAvailability::kReady};
    marks.SetWindowObservation(observed);marks.SetViewportContinuity(1,true);
    const auto finish=[&](std::uint64_t generation,double y) {
      Check(marks.BindContext(generation,context),"admitted note context binds");
      CaptureResult pixels;pixels.requested=Now();pixels.completed=Now();pixels.pixels={100,100,Bytes(40000,255)};
      marks.Captured(generation,std::move(pixels));
      marks.Release(generation,std::vector<std::vector<DisplayPoint>>{{
        {1,CoordinateUnit::kLogicalPoints,{20,y},1},{1,CoordinateUnit::kLogicalPoints,{40,y},1}}},Now());
      store.WaitForIdleForTesting();
    };
    finish(marks.Begin(root),20);marks.RecordPresentedSingle(*store.LookupMetadata(root)->metadata,context.displays);
    Check(store.UpdateMarkNote(root,StableMarkId(root,0),"original note owner",0).status==MarkNoteUpdateStatus::kUpdated,
        "owned original note persists before append");
    finish(marks.BeginAppendReference(root,1,next),60);
    GlobalVisibilitySnapshot shown;shown.state=GlobalVisibilityState::kVisible;
    STNoteVisibilitySource source;source.marks=&marks;
    source.lookup=[&](std::string_view id){return store.LookupMetadata(id);};
    source.snapshots=[&](std::string_view id){return store.MarkSnapshots(id);};
    source.collection=[&](std::string_view id){return store.Collection(id);};
    source.displays=[&]{return context.displays;};source.visibility=[&]{return shown;};
    source.screenReady=[]{return true;};source.foregroundPID=[]{return std::int64_t{0};};
    STNoteVisibilityCoordinator* validator=[[STNoteVisibilityCoordinator alloc] initWithEditor:nil source:source];
    const auto snapshot=store.MarkSnapshots(root);const auto live=marks.LiveShots(root);
    Check(live.size()==2&&STPresentedNoteBadges(store,marks,root,shown,context.displays).size()==2,
        "old and new admitted shots expose the same two original-owner number badges");
    for(const auto& shot:live)Check(STVisibleReferenceRegions(*shot.metadata,snapshot,shown).size()==1,
        "each admitted shot ink validates its own global range in whole snapshot");
    for(const auto& mark:snapshot.marks)Check([validator eligibleReference:root mark:mark.id ownFocus:NO],
        "both continuously admitted number owners are live note entry points");
    const auto originalBadges=STPresentedNoteBadges(store,marks,root,shown,context.displays);
    STBeginOwnUIPresentation(marks);const auto target=marks.own_ui_presentation_source();
    Check(target.has_value(),"actual admitted own UI transition captures exact badge-source target");
    marks.SetWindowObservation({WindowAvailability::kUnavailable,2,Now()});
    observed.generation=3;observed.observed=Now();
    Check(STPublishOwnUIPresentation(marks,*target,observed,{},true,7,7),"owned source completion is privately admitted");
    seethis::platform::ViewportContinuity scrollContinuity;
    scrollContinuity.Observe(seethis::platform::ViewportInput::kHealthy);
    const auto oldScrollEpoch=scrollContinuity.epoch();
    STObservePresentationInput(scrollContinuity,marks,seethis::platform::ViewportInput::kScroll,false,false);
    Check(scrollContinuity.epoch()>oldScrollEpoch&&marks.own_ui_presentation_source()&&
        marks.own_ui_presentation_source()->generation==target->generation&&
        STPresentedNoteBadges(store,marks,root,shown,context.displays).size()==originalBadges.size(),
        "SCROLL fixed positions: actual unowned scroll continuity preserves fresh private ink/number/hit immediately");
    const auto ownedBadges=STPresentedNoteBadges(store,marks,root,shown,context.displays);
    Check(ownedBadges.size()==originalBadges.size(),"all original numbered badge owners remain presented during own UI");
    for(std::size_t index=0;index<originalBadges.size();++index) {
      const auto& a=originalBadges[index];const auto& b=ownedBadges[index];
      Check(a.reference_id==b.reference_id&&a.mark_id==b.mark_id&&a.number==b.number&&
            a.anchor.display_id==b.anchor.display_id&&a.anchor.unit==b.anchor.unit&&
            a.anchor.backing_scale==b.anchor.backing_scale&&a.anchor.position.x==b.anchor.position.x&&
            a.anchor.position.y==b.anchor.position.y&&STNoteBadgeHit(b,a.anchor),
          "badge reference/mark/number/anchor display-unit-scale-x-y and hit stay exact");
      const std::vector<seethis::core::Rect> covered={{a.anchor.position.x-2,a.anchor.position.y-2,4,4}};
      Check(STOwnPanelCoversPoint(a.anchor,context.displays,covered)&&
            !STOwnPanelCoversPoint({1,CoordinateUnit::kLogicalPoints,{90,90},1},context.displays,covered),
          "production own-panel coverage denies covered badge and permits outside point");
    }
    marks.SetOwnInputFocus(false);observed.generation=4;observed.observed=Now();marks.SetWindowObservation(observed);
    marks.EndOwnUIPresentationSource();
    Check(STPresentedNoteBadges(store,marks,root,shown,context.displays).size()==2,"real return preserves original numbered owners");
    Check(marks.BeginPresentationPrefix()&&STPresentedNoteBadges(store,marks,root,shown,context.displays).size()==2&&
        ![validator eligibleReference:root mark:snapshot.marks[0].id ownFocus:NO],
        "pending prefix keeps numbers but suspends matching number hit eligibility");
    Check([validator validReference:root mark:snapshot.marks[0].id],"prefix never retargets persistent note owner");
    marks.CancelPresentationPrefix();
    Check(STPresentedNoteBadges(store,marks,root,shown,context.displays).size()==2&&
        [validator eligibleReference:root mark:snapshot.marks[0].id ownFocus:NO],
        "benign prefix cancellation resumes exact existing ink/number/note eligibility");
    marks.SetViewportContinuity(2,true);
    Check(STPresentedNoteBadges(store,marks,root,shown,context.displays).size()==2,
        "ordinary epoch preserves unified saved ink and numbers");
    marks.SetPresentationPermitted(false);
    Check(STPresentedNoteBadges(store,marks,root,shown,context.displays).empty()&&[validator validReference:root mark:snapshot.marks[0].id],
        "temporary global hide suppresses ink/number while persistent note owner remains valid");
    marks.SetPresentationPermitted(true);
    Check(STPresentedNoteBadges(store,marks,root,shown,context.displays).size()==2,"Show recovers exact saved badge owners");
    auto moved=marks.window_observation();moved.generation++;moved.observed=Now();moved.bounds.x++;marks.SetWindowObservation(moved);
    Check(STPresentedNoteBadges(store,marks,root,shown,context.displays).empty()&&[validator validReference:root mark:snapshot.marks[0].id],
        "known window movement blocks ink/numbers while persistent original note remains valid");
    observed.generation=moved.generation+1;observed.observed=Now();marks.SetWindowObservation(observed);
    Check(STPresentedNoteBadges(store,marks,root,shown,context.displays).size()==2&&[validator validReference:root mark:snapshot.marks[0].id],
        "fresh original bounds restore exact saved ink/numbers and persistent original note owner");
  }
  std::filesystem::remove_all(directory);
}

int main(int argc,char** argv) {
  CheckP4ContinuousNoteMembership();CheckP4OwningShotTopology();
  if(argc==2&&std::string_view(argv[1])=="--p4-component-only") {
    using namespace seethis::core;
    Reference reference;reference.id="root";reference.schema=2;reference.first_mark_slot=1;
    ReferenceRegion region;region.path={{1,CoordinateUnit::kLogicalPoints,{20,20},1},{1,CoordinateUnit::kLogicalPoints,{40,40},1}};
    reference.regions={region};
    MarkNotesSnapshot snapshot;snapshot.status=MarkNotesStatus::kReady;
    MarkSnapshot old;old.slot=0;old.id=StableMarkId(reference.id,0);
    MarkSnapshot fresh;fresh.slot=1;fresh.number=2;fresh.id=StableMarkId(reference.id,1);
    snapshot.marks={old,fresh};
    GlobalVisibilitySnapshot visible;visible.state=GlobalVisibilityState::kVisible;
    Check(STVisibleReferenceRegions(reference,snapshot,visible).size()==1,"latest shot stroke binds global offset");
    GlobalVisibilitySnapshot hidden;hidden.state=GlobalVisibilityState::kInvisible;
    Check(STVisibleReferenceRegions(reference,snapshot,hidden).empty(),"global hide suppresses latest stroke");
    snapshot.status=MarkNotesStatus::kCorrupt;
    Check(STVisibleReferenceRegions(reference,snapshot,visible).empty(),"corrupt ownership fails closed");
    std::cout<<"P4 visibility pure component checks passed\n";return 0;
  }
  @autoreleasepool {
    [NSApplication sharedApplication];
    using namespace seethis::core;
    const std::string reference_id(32,'a');
    FrozenDisplay display;
    display.uuid="display-1";display.id=1;display.scale=1;
    display.logical={0,0,100,100};display.pixels={0,0,100,100};
    Reference reference;reference.id=reference_id;
    reference.context.displays={display};
    const auto point=[](double x,double y) {
      return DisplayPoint{1,CoordinateUnit::kLogicalPoints,{x,y},1};
    };
    ReferenceRegion first;
    first.subpaths={{point(10,10),point(20,10)},
                    {point(40,10),point(50,10)}};
    ReferenceRegion second;
    second.path={point(70,60),point(80,70),point(70,80),point(60,70),point(70,60)};
    reference.regions={first,second};
    MarkNotesSnapshot snapshot;
    snapshot.status=MarkNotesStatus::kReady;
    snapshot.marks={
        {StableMarkId(reference_id,0),0,1,point(10,10),{},"first",false,1},
        {StableMarkId(reference_id,1),1,2,point(70,60),{},"second",true,1}};
    GlobalVisibilitySnapshot global{GlobalVisibilityState::kVisible};
    auto visible=STVisibleReferenceRegions(reference,snapshot,global);
    Check(visible.size()==2&&visible[0].subpaths.size()==2&&
          STRegionsHit(reference.context,visible,point(15,10),2,{display})&&
          STRegionsHit(reference.context,visible,point(70,60),2,{display})&&
          !STRegionsHit(reference.context,visible,point(30,10),2,{display}),
          "global Visible restores every mixed-bit gesture and preserves clipped subpaths");
    const auto badges=STReferenceNoteBadges(reference_id,snapshot,global);
    Check(badges.size()==2&&badges[0].number==1&&badges[1].number==2&&
          STNoteBadgeHit(badges[0],point(10,10)),
          "production badge projection restores every stable numbered hit target");
    for(const auto state:{GlobalVisibilityState::kInvisible,GlobalVisibilityState::kUncertain,
                          GlobalVisibilityState::kUnavailable}) {
      global.state=state;visible=STVisibleReferenceRegions(reference,snapshot,global);
      Check(visible.empty()&&STReferenceNoteBadges(reference_id,snapshot,global).empty()&&
            !STRegionsHit(reference.context,visible,point(70,60),2,{display}),
            "Invisible, uncertain and unavailable suppress ink, badge and hit presentation");
    }
    global.state=GlobalVisibilityState::kVisible;
    snapshot.marks[1].visible=false;
    Check(STVisibleReferenceRegions(reference,snapshot,global).size()==2&&
          !snapshot.marks[0].visible&&!snapshot.marks[1].visible,
          "all historical hidden bits remain dormant and unmodified");
    snapshot.status=MarkNotesStatus::kCorrupt;
    Check(STVisibleReferenceRegions(reference,snapshot,global).empty()&&
          STReferenceNoteBadges(reference_id,snapshot,global).empty(),
          "unreadable metadata still suppresses presentation under Visible");

    STNoteRead read=[](std::string_view,std::string_view)->STNoteReadResult {
      return {STNoteReadStatus::kReady,"Saved note",0};
    };
    STNoteSave save=[](std::string_view,std::string_view,std::string_view,
                       std::uint64_t)->STNoteSaveResult {
      return {STNoteSaveStatus::kFailed,0};
    };
    STMarkNoteEditor* editor=[[STMarkNoteEditor alloc]
        initWithRead:read save:save copy:[](std::string_view)->STNoteCopyStatus {return STNoteCopyStatus::kCopied;}];
    [editor toggleReference:reference_id mark:snapshot.marks[0].id
                     number:1 screenPoint:NSMakePoint(300,300)];
    editor.textView.string=@"Unsaved draft";
    [editor textDidChange:[NSNotification notificationWithName:
        NSTextDidChangeNotification object:editor.textView]];
    [editor hideIfEditingReference:reference_id mark:snapshot.marks[1].id];
    Check(editor.panel.isVisible,"another mark cannot hide the selected editor");
    [editor hideIfEditingReference:reference_id mark:snapshot.marks[0].id];
    Check(!editor.panel.isVisible,
          "hiding the edited mark dismisses its editor after failed save");
    [editor toggleReference:reference_id mark:snapshot.marks[0].id
                     number:1 screenPoint:NSMakePoint(300,300)];
    Check([editor.textView.string isEqualToString:@"Unsaved draft"]&&
          !editor.retryButton.hidden,
          "hidden editor retains its unsaved draft for restoration");
    [editor hide];
    CheckPersistentContext();CheckChromeTransitions();CheckRetirementAndDrafts();
#if defined(SEETHIS_NCV_FULL_COORDINATOR_TESTING)
    CheckFixedActions();CheckProductionInvalidationWiring();CheckProductionInvalidationWiring(true);
#endif
  }
  std::cout<<"Inspector note visibility tests passed: "<<checks<<" checks\n";
  return 0;
}
