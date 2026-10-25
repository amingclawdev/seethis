#define SEETHIS_INSPECTOR_REFERENCE_ACTIONS_TESTING 1
#import "../src/platform/mac/overlay_mac.mm"
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>

// This isolated overlay test does not link the production input adapter.
namespace seethis::platform {
EffectiveShortcutSnapshot InputShortcutStatus() { return {}; }
ChromeConnectionSnapshot ChromeConnectionStatus() { return {}; }
void ConnectChromeExplicit() {}
void RetryChromeExplicit() {}
bool OpenChromeAutomationSettingsExplicit() { return false; }
}  // namespace seethis::platform

using seethis::core::DeleteResult;
using seethis::core::ReferenceJobSnapshot;
using seethis::core::ReferenceJobState;
using seethis::core::MarkSnapshot;

static void Check(bool condition,const char* message) {
  if(!condition){std::cerr<<message<<'\n';std::exit(1);}
}
static ReferenceJobSnapshot Job(const char* id,ReferenceJobState state) {
  ReferenceJobSnapshot job;job.id=id;job.state=state;return job;
}



@interface STSourceLabelTarget : NSObject <STInspectorReferenceTarget>
@property(nonatomic,copy) NSString* chosen;
@end
@implementation STSourceLabelTarget
- (void)inspectorSelectReference:(NSString*)identifier {self.chosen=identifier;}
@end

static void TestPersistedCapturedLabel() {
  using namespace seethis::core;
  char pattern[]="/tmp/seethis-cjc-label-XXXXXX";
  const char* directory=mkdtemp(pattern);Check(directory,"label fixture directory exists");
  const auto id=std::string(32,'e');
  NSString* frozen;
  {
    ReferenceStore store(directory);
    MarkController marks(store,[](const auto&){return true;});
    const auto observed=Now();
    Context context;context.pid=42;context.window_pid=42;context.self_pid=99;
    context.window_id=7;context.app_name="Google Chrome";context.bundle_id="com.google.Chrome";
    context.executable="/fixture";context.window_title="pre-overlay native title";
    context.window={0,0,100,100};context.quartz_to_appkit_top=100;context.observed=observed;
    context.selection_method="frontmost-pid/topmost-normal-containing-initial-point";
    context.excluded_window_ids={8};context.exclusion_method="exact-window-allowlist;self-pid-rejected;child-windows-off";
    context.displays.push_back({"display-a",1,{0,0,100,100},{0,0,100,100},1,{1,-1,0,100}});
    WindowObservation foreground;foreground.availability=WindowAvailability::kReady;
    foreground.generation=1;foreground.observed=observed;foreground.pid=42;
    foreground.bundle_id=context.bundle_id;foreground.window_id=7;foreground.bounds=context.window;
    marks.SetWindowObservation(foreground);
    const auto generation=marks.Begin(id);Check(marks.BindContext(generation,context),"label context binds");
    PageIdentity page;page.bundle_id=context.bundle_id;page.browser_pid=42;
    page.window_id=7;page.window_bounds=context.window;page.process_start_identity_us=700;
    page.opaque_tab_id="frozen-tab";
    const std::string url="https://example.test/Persisted";
    page.navigation_digest=seethis::platform::ChromeNavigationDigest(url);
    Check(marks.BindPageIdentity(generation,page,CapturedPageMetadata{url,
        SourceFieldAvailability::kCaptured,observed.utc_us+5}),"label captured page binds");
    CaptureResult capture;capture.requested={observed.utc_us+10,observed.monotonic_us+10};
    capture.completed={observed.utc_us+20,observed.monotonic_us+20};
    capture.pixels={100,100,Bytes(100*100*4,255)};
    marks.Captured(generation,std::move(capture));
    marks.Release(generation,{{1,CoordinateUnit::kLogicalPoints,{20,20},1},
        {1,CoordinateUnit::kLogicalPoints,{40,40},1}},
        {observed.utc_us+30,observed.monotonic_us+30});
    store.WaitForIdleForTesting();
    const auto job=store.LookupMetadata(id);
    Check(job&&job->state==ReferenceJobState::kReady,"label capture is durably ready");
    frozen=InspectorReferenceLabel(*job).full;
    Check([frozen hasPrefix:@"https://example.test/Persisted — Chrome — "],
        "production label reads same persisted captured URL after actual capture completion");
    page.navigation_digest=seethis::platform::ChromeNavigationDigest("https://example.test/later");
    marks.SetPageObservation({PageAvailability::kReady,2,Now(),page});
    Check([InspectorReferenceLabel(*store.LookupMetadata(id)).full isEqual:frozen],
        "same native window navigation changes current observation but leaves historical label frozen");
  }
  {
    ReferenceStore restarted(directory);const auto job=restarted.LookupMetadata(id);
    Check(job&&[InspectorReferenceLabel(*job).full isEqual:frozen],
        "same reference ID restores exact production label and image completion after restart");
  }
  [[NSFileManager defaultManager] removeItemAtPath:[NSString stringWithUTF8String:directory] error:nil];
}

static void TestCapturedReferenceLabels() {
  using namespace seethis::core;
  Reference reference;
  reference.id=std::string(32,'a');
  reference.context.bundle_id="com.google.Chrome";
  reference.context.app_name="Google Chrome";
  reference.context.window_title="current page title must not replace captured URL";
  reference.context.pid=42;reference.context.window_pid=42;reference.context.self_pid=99;
  reference.context.window_id=7;
  reference.context.window={0,0,100,100};
  reference.context.observed={1'700'000'000'000'000,100};
  reference.image_completed={1'710'000'000'000'000,120};
  reference.captured_page=CapturedPageMetadata{std::string("https://example.test/Frozen?q=é"),
      SourceFieldAvailability::kCaptured,1'700'000'000'000'005};
  auto snapshot=[&](const Reference& value) {
    ReferenceJobSnapshot job;job.id=value.id;job.state=ReferenceJobState::kReady;
    job.context=value.context;job.captured_page=value.captured_page;
    job.accepted_utc_us=1'720'000'000'000'000;
    auto bound=value;
    if(bound.captured_page && bound.captured_page->url) {
      PageIdentity page;page.bundle_id=bound.context.bundle_id;page.browser_pid=42;
      page.window_id=bound.context.window_id;page.window_bounds=bound.context.window;
      page.process_start_identity_us=700;page.opaque_tab_id="fixture-tab";
      page.navigation_digest=seethis::platform::ChromeNavigationDigest(*bound.captured_page->url);
      bound.page_identity=page;
    }
    job.metadata=std::make_shared<const Reference>(bound);
    return job;
  };
  const auto original=snapshot(reference);
  const auto label=InspectorReferenceLabel(original);
  NSString* completed=TimestampText(reference.image_completed.utc_us);
  NSString* expected=[NSString stringWithFormat:@"https://example.test/Frozen?q=é — Chrome — %@",completed];
  Check([label.full isEqualToString:expected] && [label.time isEqualToString:completed] &&
        ![label.full containsString:TimestampText(original.accepted_utc_us)] &&
        ![label.full containsString:@"current page title"],
      "production label uses exact captured URL and localized image completion, never accepted/current title");
  auto later=reference;later.context.window_id=8;
  later.captured_page->url="https://example.test/Navigated";
  later.image_completed.utc_us+=1'000'000;
  const auto laterLabel=InspectorReferenceLabel(snapshot(later));
  Check([InspectorReferenceLabel(original).full isEqualToString:label.full] &&
        ![laterLabel.full isEqualToString:label.full],
      "a different current page/window cannot rewrite the immutable original label");
  reference.captured_page->url="https://example.test/"+std::string(12000,'x')+"é";
  const auto longLabel=InspectorReferenceLabel(snapshot(reference));
  Check([longLabel.row containsString:@"… — Chrome — "] &&
        longLabel.row.length<longLabel.full.length &&
        [longLabel.full containsString:[NSString stringWithUTF8String:reference.captured_page->url->c_str()]] &&
        [longLabel.row sizeWithAttributes:@{NSFontAttributeName:[NSFont systemFontOfSize:12]}].width<=431,
      "long captured URL shortens by composed characters while Chrome and capture time remain readable");
  auto legacy=reference;legacy.captured_page.reset();
  const auto missing=InspectorReferenceLabel(snapshot(legacy));
  Check([missing.full hasPrefix:@"Chrome (captured URL unavailable) — "] &&
        ![missing.full containsString:@"https://"],
      "missing legacy URL has an honest Chrome/time label");
  for(const auto& invalid:std::vector<std::string>{std::string(16385,'x'),
      std::string("a\0b",3),"a\nb",std::string("\xc0\xaf",2)}) {
    auto invalidReference=reference;invalidReference.captured_page->url=invalid;
    Check([InspectorReferenceLabel(snapshot(invalidReference)).full hasPrefix:
        @"Chrome (captured URL unavailable) — "],
        "over-limit or invalid URL label uses the same bounded projection as JSON");
  }
  auto no_time=legacy;no_time.image_completed.utc_us=0;
  Check([InspectorReferenceLabel(snapshot(no_time)).time isEqualToString:@"Capture time unavailable"],
      "missing image completion never borrows accepted or current time");
  ReferenceJobSnapshot pending;pending.id="pending";pending.context=legacy.context;
  pending.state=ReferenceJobState::kPending;pending.accepted_utc_us=original.accepted_utc_us;
  Check([InspectorReferenceLabel(pending).time isEqualToString:@"Capture time unavailable"],
      "pending capture does not masquerade as completed screenshot time");
  auto non_chrome=reference;non_chrome.captured_page.reset();
  non_chrome.context.bundle_id="com.example.Codex";non_chrome.context.app_name="Codex";
  non_chrome.context.window_title="Design review";
  Check([InspectorReferenceLabel(snapshot(non_chrome)).full hasPrefix:@"Design review — Codex — "],
      "Codex title and app remain readable from the frozen projection");
  non_chrome.context.app_name="Discord";non_chrome.context.window_title="Team channel";
  Check([InspectorReferenceLabel(snapshot(non_chrome)).full hasPrefix:@"Team channel — Discord — "],
      "Discord title and app remain readable without claiming unique session identity");
  non_chrome.context.window_title=std::string("bad\nmetadata");
  Check([InspectorReferenceLabel(snapshot(non_chrome)).full hasPrefix:@"Discord — "],
      "invalid title omits only that field and retains the readable captured app");
  // These are the actual native menu objects and action path used in production.
  STInspectorReferenceSelector* selector=[[STInspectorReferenceSelector alloc] initWithFrame:NSZeroRect];
  STSourceLabelTarget* target=[STSourceLabelTarget new];selector.target=target;
  selector.referenceIDs=@[@"reference-a",@"reference-b"];
  selector.referenceRows=@[longLabel.row,longLabel.row];
  selector.referenceFullRows=@[longLabel.full,longLabel.full];
  selector.referenceTimes=@[longLabel.time,longLabel.time];
  selector.selectedReferenceID=@"reference-a";[selector refreshMenu];
  NSMenuItem* selected=selector.popup.selectedItem;
  Check(selector.popup.numberOfItems==3 &&
        [selector.popup.itemArray[2].representedObject isEqual:@"reference-b"] &&
        [selected.representedObject isEqual:@"reference-a"] &&
        [selected.toolTip isEqual:longLabel.full] &&
        [selected.accessibilityLabel isEqual:longLabel.full] &&
        [selector.popup.toolTip isEqual:longLabel.full] &&
        [selector.popup.accessibilityValue isEqual:longLabel.full],
      "visually shortened URL remains exact in native tooltip and accessibility while identity remains reference ID");
  [selector chooseReference:selector.popup.itemArray[2]];
  Check([target.chosen isEqual:@"reference-b"],
      "equal URL/title labels still dispatch the selected stable representedObject ID");
  [selector menuWillOpen:selector.popup.menu];
  NSMenuItem* displayed=selector.popup.itemArray[1];
  selector.referenceIDs=@[@"reference-b",@"reference-a"];
  [selector refreshMenu];[selector chooseReference:displayed];
  Check([target.chosen isEqual:@"reference-a"],
      "arrival reordering during menu tracking preserves displayed reference action identity");
  [selector menuDidClose:selector.popup.menu];
}

static NSBitmapImageRep* RasterPreview(NSImage* source,const STPreviewNumbers& data,
    NSSize size,CGFloat scale,NSString* key) {
  NSBitmapImageRep* bitmap=[[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr
      pixelsWide:lround(size.width*scale) pixelsHigh:lround(size.height*scale)
      bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO
      colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
  [NSGraphicsContext saveGraphicsState];
  NSGraphicsContext* context=[NSGraphicsContext graphicsContextWithBitmapImageRep:bitmap];
  CGContextRef cg=context.CGContext;
  CGContextScaleCTM(cg,scale,scale);CGContextTranslateCTM(cg,0,size.height);CGContextScaleCTM(cg,1,-1);
  [NSGraphicsContext setCurrentContext:[NSGraphicsContext graphicsContextWithCGContext:cg flipped:YES]];
  [NSColor.blackColor setFill];NSRectFill(NSMakeRect(0,0,size.width,size.height));
  InspectorDrawPreview(source,NSMakeRect(0,0,size.width,size.height),data,key);
  [NSGraphicsContext restoreGraphicsState];return bitmap;
}
static void SavePreview(NSBitmapImageRep* bitmap,NSString* name) {
  const char* directory=getenv("SEETHIS_TEST_ARTIFACT_DIR");if(!directory)return;
  NSString* folder=[NSString stringWithUTF8String:directory];
  [[NSFileManager defaultManager] createDirectoryAtPath:folder withIntermediateDirectories:YES attributes:nil error:nil];
  [[bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}]
      writeToFile:[folder stringByAppendingPathComponent:name] atomically:YES];
}
static void TestSourcePngPreview() {
  // Optional cross-lane pixel fixture, never a substitute for combined-candidate QA.
  const char* pngPath=getenv("SEETHIS_TEST_PREVIEW_UNDERLAY_PNG");
  const char* geometryPath=getenv("SEETHIS_TEST_PREVIEW_UNDERLAY_GEOMETRY");
  if(!pngPath||!geometryPath)return;
  NSImage* source=[[NSImage alloc] initWithContentsOfFile:[NSString stringWithUTF8String:pngPath]];
  NSDictionary* geometry=[NSJSONSerialization JSONObjectWithData:
      [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:geometryPath]] options:0 error:nil];
  Check(source && geometry,"core underlay PNG/geometry decodes");
  NSData* bytes=[NSData dataWithContentsOfFile:[NSString stringWithUTF8String:pngPath]];
  seethis::core::Bytes owned(static_cast<const std::uint8_t*>(bytes.bytes),
      static_cast<const std::uint8_t*>(bytes.bytes)+bytes.length);
  Check([geometry[@"representation"] isEqual:@"strokes-only-v1"] &&
        [geometry[@"geometry_version"] isEqual:[NSString stringWithUTF8String:
          std::string(seethis::core::ReferenceStore::kDisplayUnderlayGeometryVersion).c_str()]] &&
        seethis::core::Sha256(owned)==std::string([geometry[@"sha256"] UTF8String]),
      "actual underlay fixture representation, shared version and bytes match descriptor");
  STPreviewNumbers data;data.key="source:digest";
  data.source=NSMakeSize([geometry[@"width"] doubleValue],[geometry[@"height"] doubleValue]);
  source.size=data.source;
  for(NSDictionary* mark in geometry[@"marks"]) {
    NSArray* anchor=mark[@"source_anchor"];
    data.marks.push_back({[mark[@"number"] unsignedIntValue],
      NSMakePoint([anchor[0] doubleValue],[anchor[1] doubleValue])});
  }
  data.digest=seethis::core::Sha256(owned);
  data.geometryVersion=std::string(seethis::core::ReferenceStore::kDisplayUnderlayGeometryVersion);
  const auto immutable=std::make_shared<const seethis::core::Bytes>(owned);
  CGImageRef thumbnail=InspectorDecodePreview(immutable,data);
  Check(thumbnail && CGImageGetWidth(thumbnail)<=1024 && CGImageGetHeight(thumbnail)<=1024,
      "production owned-byte thumbnail decoder accepts actual core fixture within bounds");
  NSImage* decoded=[[NSImage alloc] initWithCGImage:thumbnail size:data.source];
  CGImageRelease(thumbnail);
  Check(fabs(decoded.size.width/decoded.size.height-data.source.width/data.source.height)<1e-12,
      "rounded thumbnail backing pixels retain exact immutable source aspect ratio");
  auto invalid=data;invalid.digest=std::string(64,'0');
  Check(!InspectorDecodePreview(immutable,invalid),"production decoder rejects wrong expected digest");
  invalid=data;invalid.source.width++;
  Check(!InspectorDecodePreview(immutable,invalid),"production decoder rejects decoded dimension conflict");
  invalid=data;invalid.geometryVersion="unsupported";
  Check(!InspectorDecodePreview(immutable,invalid),"production decoder rejects unsupported geometry version");
  source=decoded;
  Check(InspectorPreviewCallouts(data,NSMakeRect(0,0,438,180)).size()==data.marks.size(),
      "production PNG fixture gets every source mark in actual Inspector frame");
  SavePreview(RasterPreview(source,data,NSMakeSize(438,180),2,@"source:digest"),@"inspector-source-png-fit.png");
  Check(InspectorPreviewCallouts(data,NSMakeRect(0,0,1920,1080)).size()==data.marks.size(),
      "real underlay keeps one complete display layer at natural scale");
  SavePreview(RasterPreview(source,data,NSMakeSize(1920,1080),1,@"source:digest"),@"inspector-source-png-natural.png");
  SavePreview(RasterPreview(source,data,NSMakeSize(1900,1068),1,@"source:digest"),@"inspector-source-png-below-threshold.png");
}

static void TestPreviewNumbers() {
  seethis::core::Reference reference;
  reference.schema=2;reference.id="retina";reference.source.width=3840;reference.source.height=2160;
  reference.global_to_source={2,-2,0,2160};
  reference.context.displays.push_back({"retina",1,{0,0,1920,1080},{0,0,3840,2160},2,{2,-2,0,2160}});
  seethis::core::MarkNotesSnapshot snapshot;snapshot.status=seethis::core::MarkNotesStatus::kReady;
  const std::vector<NSPoint> positions={{0,0},{20,0},{40,0},{60,0},{80,0},{100,0},
      {1800,900},{1820,900},{1840,900},{1860,900},{1880,900},{1900,900}};
  for(std::size_t i=0;i<positions.size();++i) {
    MarkSnapshot mark;mark.number=i<6?i+1:i+6;mark.slot=mark.number-1;
    mark.id=seethis::core::StableMarkId(reference.id,mark.slot);
    mark.anchor={1,seethis::core::CoordinateUnit::kLogicalPoints,
      {positions[i].x/2,1080-positions[i].y/2},2};
    // Circle starts at its right edge, deliberately distinct from crop origin.
    mark.delivery_bounds={positions[i].x-80,positions[i].y-80,160,160};
    snapshot.marks.push_back(mark);
  }
  Check(InspectorPreviewNumbers(reference,snapshot,"invalid").marks.empty(),
      "sparse helper subset is not accepted as complete production marks");
  auto invalid=snapshot;invalid.marks[1].number=99;
  Check(InspectorPreviewNumbers(reference,invalid,"invalid",false).marks.empty(),
      "immutable number must equal original slot plus one");
  invalid=snapshot;invalid.marks[1].id=invalid.marks[0].id;
  Check(InspectorPreviewNumbers(reference,invalid,"invalid",false).marks.empty(),
      "duplicate stable mark IDs cannot bind a native model");
  const auto data=InspectorPreviewNumbers(reference,snapshot,"retina:digest",false);
  Check(data.marks.size()==12,"explicit sparse model retains immutable IDs and original numbers");
  Check(data.marks[6].source.x==1800&&data.marks[6].source.y==900&&data.marks[6].number==12,
      "circle leader uses drawing start, not delivery rectangle or array number");
  const NSRect viewport=NSMakeRect(0,0,438,180);
  const auto labels=InspectorPreviewCallouts(data,viewport);
  Check(labels.size()==data.marks.size(),"dense adjacent/multidigit preview retains every number");
  for(std::size_t i=0;i<labels.size();++i) {
    Check(NSContainsRect(NSInsetRect(viewport,1,1),labels[i].bounds),"label rim/glyph is inside viewport");
    Check(fabs(labels[i].anchor.x-(59+data.marks[i].source.x/12))<0.01&&
          fabs(labels[i].anchor.y-data.marks[i].source.y/12)<0.01,
          "leader endpoint maps exact start into real320x180 fitted image");
    for(std::size_t j=0;j<labels.size();++j)if(i!=j) {
      Check(!NSIntersectsRect(labels[i].bounds,labels[j].bounds),"dense labels do not overlap");
      for(std::size_t n=1;n<labels[i].leader.size();++n)
        Check(!InspectorSegmentHits(labels[i].leader[n-1],labels[i].leader[n],
          labels[j].bounds),"leader cannot cross another number");
    }
  }
  Check(InspectorPreviewCallouts(data,NSMakeRect(0,0,1920,1080)).size()==12,
      "natural logical preview keeps one complete display layer");
  Check(!InspectorPreviewCallouts(data,NSMakeRect(0,0,1900,1068)).empty(),
      "former threshold width keeps the same complete number layer");
  const auto tall=[&]{auto d=data;d.source=NSMakeSize(2160,3840);return d;}();
  Check(InspectorPreviewCallouts(tall,viewport).size()==12,"tall letterboxed image retains dense numbers");
  Check(InspectorPreviewCallouts(data,NSMakeRect(0,0,20,20)).empty(),
      "finite viewport reports insufficient layout capacity");
  Check(InspectorPreviewCompletionMatches(true,true,"a","a","digest",@"digest",@"a:digest",@"a:digest"),
      "ready exact selected reference/digest completion is accepted");
  Check(!InspectorPreviewCompletionMatches(false,true,"a","a","digest",@"digest",@"a:digest",@"a:digest") &&
      !InspectorPreviewCompletionMatches(true,false,"a","a","digest",@"digest",@"a:digest",@"a:digest") &&
      !InspectorPreviewCompletionMatches(true,true,"a","b","digest",@"digest",@"a:digest",@"a:digest") &&
      !InspectorPreviewCompletionMatches(true,true,"a","a","new",@"old",@"a:new",@"a:old") &&
      !InspectorPreviewCompletionMatches(true,true,"a","a","digest",@"digest",@"b:digest",@"a:digest"),
      "selection generation, pending/failure/deletion, reference and digest/key changes reject stale completion");
  NSImage* source=[[NSImage alloc] initWithSize:NSMakeSize(3840,2160)];
  [source lockFocus];[[NSColor colorWithWhite:0.35 alpha:1] setFill];
  NSRectFill(NSMakeRect(0,0,3840,2160));[source unlockFocus];
  NSBitmapImageRep* bitmap=RasterPreview(source,data,NSMakeSize(438,180),2,@"retina:digest");
  SavePreview(bitmap,@"inspector-retina-dense-438x180-at2x.png");
  for(const auto& label:labels) {
    // Exclude the white rim and measure the real text's white connected extent.
    int minimum=9999,maximum=-1,count=0;
    for(int y=(label.bounds.origin.y+4)*2;y<(NSMaxY(label.bounds)-4)*2;++y)
      for(int x=(label.bounds.origin.x+4)*2;x<(NSMaxX(label.bounds)-4)*2;++x) {
        NSColor* color=[bitmap colorAtX:x y:y];
        if(color.redComponent>0.9&&color.greenComponent>0.9&&color.blueComponent>0.9)
          {minimum=std::min(minimum,y);maximum=std::max(maximum,y);++count;}
      }
    if(count<25||maximum-minimum<16)std::cerr<<"number="<<label.number<<" ink="<<count<<" height="<<maximum-minimum<<" box="<<NSMinX(label.bounds)<<","<<NSMinY(label.bounds)<<"\n";
    Check(count>=25&&maximum-minimum>=16,"each numeral has readable real AppKit glyph height and interior ink");
  }
  auto stale=data;stale.key="other:digest";
  auto empty=data;empty.marks.clear();empty.key="other:digest";
  NSBitmapImageRep* stalePixels=RasterPreview(source,stale,NSMakeSize(438,180),2,@"retina:digest");
  NSBitmapImageRep* emptyPixels=RasterPreview(source,empty,NSMakeSize(438,180),2,@"retina:digest");
  Check(std::equal(stalePixels.bitmapData,stalePixels.bitmapData+
      stalePixels.bytesPerRow*stalePixels.pixelsHigh,emptyPixels.bitmapData),
      "stale binding draws no cross-reference pixels or labels");
  for(auto status:{seethis::core::MarkNotesStatus::kPending,seethis::core::MarkNotesStatus::kFailed,
      seethis::core::MarkNotesStatus::kDeleted,seethis::core::MarkNotesStatus::kExpired}) {
    snapshot.status=status;
    Check(InspectorPreviewNumbers(reference,snapshot,"unavailable").marks.empty(),
        "pending/failure/deletion/expiry reset number data");
  }
}

// Uses the same hit, selection and shortcut boundary as production. No native
// panel, event replay, general pasteboard or external reference data is created.
static void TestStrokeSelectionCaptureFlow() {
  using namespace seethis::core;
  char pattern[]="/tmp/seethis-p4-stroke-selection-XXXXXX";
  const char* directory=mkdtemp(pattern);Check(directory,"owned stroke directory");
  {
    ReferenceStore store(directory);unsigned copies=0;
    MarkController marks(store,[&](const auto&){++copies;return true;});
    Context context;context.pid=42;context.window_pid=42;context.self_pid=99;context.window_id=7;
    context.app_name="Fixture";context.bundle_id="test.fixture";context.executable="/fixture";
    context.window_title="Fixture window";context.window={0,0,100,100};context.quartz_to_appkit_top=100;
    context.observed=Now();context.native_window={1,700,"append-fixture",1};context.selection_method="frontmost-pid/topmost-normal-containing-initial-point";
    context.excluded_window_ids={8};context.exclusion_method="exact-window-allowlist;self-pid-rejected;child-windows-off";
    context.displays.push_back({"display-a",1,{0,0,100,100},{0,0,100,100},1,{1,-1,0,100}});
    auto observe=[&](const Context& value) {
      WindowObservation observation;observation.availability=WindowAvailability::kReady;
      observation.generation=marks.window_observation().generation+1;observation.observed=Now();
      observation.pid=value.window_pid;observation.bundle_id=value.bundle_id;
      observation.window_id=value.window_id;observation.bounds=value.window;
      observation.native_window=value.native_window;observation.window_title=value.window_title;observation.title={1,TitleAvailability::kReady};
      marks.SetWindowObservation(observation);
    };
    const auto point=[](double x,double y){return DisplayPoint{1,CoordinateUnit::kLogicalPoints,{x,y},1};};
    const std::vector<DisplayPoint> circle={point(20,20),point(40,20),point(40,40),point(20,40),point(20,20)};
    const std::vector<DisplayPoint> line={point(50,20),point(80,20)};
    const std::vector<DisplayPoint> nextLine={point(50,60),point(80,60)};
    auto complete=[&](const STCaptureSelectionStart& start,const std::vector<std::vector<DisplayPoint>>& paths) {
      Check(start.generation&&marks.BindContext(start.generation,context),"entry-derived capture binds");
      CaptureResult capture;capture.requested=Now();capture.completed=Now();
      capture.pixels={100,100,Bytes(100*100*4,255)};
      marks.Captured(start.generation,std::move(capture));
      marks.Release(start.generation,paths,Now());store.WaitForIdleForTesting();
      Check(marks.phase()==ReferencePhase::kCommitted,"entry-derived capture commits");
    };
    const std::string root(32,'a'),shot2(32,'b'),shot3(32,'c'),other(32,'d');
    seethis::platform::ViewportContinuity continuity;
    continuity.Observe(seethis::platform::ViewportInput::kHealthy);
    const auto keyEpoch=continuity.epoch();
    continuity.Observe(seethis::platform::ViewportInput::kKeyboard,
        STOwnPanelKeyboardInput(true,true,true,false,false));
    Check(continuity.epoch()==keyEpoch&&continuity.healthy(),
        "production positive nonactivating note key-responder policy preserves source epoch without self-front assumption");
    continuity.Observe(seethis::platform::ViewportInput::kKeyboard,
        STOwnPanelKeyboardInput(false,false,false,true,true));
    Check(continuity.epoch()==keyEpoch&&!STOwnPanelKeyboardInput(true,false,true,false,false)&&
          !STOwnPanelKeyboardInput(true,true,false,true,false),
        "exact visible owned Inspector key input qualifies; hidden note and unknown/external key destination do not");
    observe(context);marks.SetViewportContinuity(continuity.epoch(),continuity.healthy());
    seethis::platform::AppendCaptureMode mode;
    complete(STBeginModeCapture(marks,store,mode,root,8,0),{circle,line,line});
    marks.RecordPresentedSingle(*store.LookupMetadata(root)->metadata,context.displays);
    const auto firstBytes=*store.ReadReadyAsset(root,false);
    GlobalVisibilitySnapshot visible;visible.state=GlobalVisibilityState::kVisible;
    const auto hit=[&](DisplayPoint p,double radius=1) {
      return STVisibleStrokeHit(store,marks,visible,context.displays,p,radius);
    };
    const auto ownStroke=[&](bool acceptingOverlay,DisplayPoint p) {
      return STOwnStrokeSelectionInput(acceptingOverlay,store,marks,visible,context.displays,p,1);
    };
    const auto circleHit=hit(point(20,30)),lineHit=hit(point(60,20));
    Check(circleHit&&circleHit->markID==StableMarkId(root,0)&&circleHit->number==1,
        "circle segment selects authoritative first mark");
    Check(lineHit&&lineHit->markID==StableMarkId(root,1)&&lineHit->localSlot==1&&lineHit->number==2,
        "line resolves distinct mark; overlapping regions retain first-local order");
    Check(hit(point(60,21))&&!hit(point(60,21.01)),"same inclusive segment radius tolerance");
    Check(!hit(point(30,30))&&!hit(point(95,95)),"circle interior and blank do not guess marks");
    seethis::platform::InspectorLifecycleState lifecycle;
    std::string selected,previewRoot,previewShot;
    STSelectStrokeHit(*lineHit,lifecycle,selected,previewRoot,previewShot);
    Check(selected==lineHit->markID&&lifecycle.selected_id==root&&previewShot==root,
        "actual stroke selection arms stable mark and owning preview");
    const auto oldSelection=selected;
    if(const auto blank=hit(point(95,95)))STSelectStrokeHit(*blank,lifecycle,selected,previewRoot,previewShot);
    Check(selected==oldSelection,"blank preserves existing intention");
    Check(store.UpdateMarkNote(root,circleHit->markID,"original exact note",0).status==MarkNoteUpdateStatus::kUpdated,
        "original note saved in owned fixture");
    const auto originalShot=store.Collection(root).shots.front();
    const auto originalMetadata=Serialize(*originalShot.metadata);
    STBeginOwnUIPresentation(marks);
    const auto singleTarget=marks.own_ui_presentation_source();
    Check(singleTarget.has_value(),"production own UI entry retains actual painted single query before ON");
    marks.SetWindowObservation({WindowAvailability::kUnavailable,marks.window_observation().generation+1,Now()});
    auto bound=marks.window_observation();bound.availability=WindowAvailability::kReady;
    bound.generation++;bound.observed=Now();bound.pid=context.pid;bound.bundle_id=context.bundle_id;
    bound.window_id=context.window_id;bound.bounds=context.window;bound.native_window=context.native_window;
    bound.window_title=context.window_title;bound.title={1,TitleAvailability::kReady};
    Check(STPublishOwnUIPresentation(marks,*singleTarget,bound,{},true,1,1)&&hit(point(20,30)),
        "production owned completion preserves exact first single before conversion");
    marks.SetOwnInputFocus(false);observe(context);marks.EndOwnUIPresentationSource();
    Check(STOwnPresentationPanelLevel()==NSStatusWindowLevel+1&&STOwnPresentationPanelLevel()>NSStatusWindowLevel,
        "actual Inspector and note initializer policy places both controls above the overlay");
    unsigned toggleRefreshes=0;
    const auto toggle=[&] {
      STToggleInspectorAppendMode(mode,store,lifecycle,[&](NSString* status) {
        ++toggleRefreshes;Check(status==nil,"ready native toggle clears status and dispatches Inspector refresh");
        Check(!STValidateAppendMode(mode,store,marks),"actual Inspector refresh validation retains valid bound root");
        Check(Serialize(*store.Collection(root).shots.front().metadata)==originalMetadata&&hit(point(20,30)),
            "native toggle/selection/refresh-validation boundary preserves exact original geometry and hit");
      });
    };
    const auto toggleGeneration=lifecycle.selection_generation;toggle();
    Check(mode.enabled()&&mode.root==root&&toggleRefreshes==1&&
          lifecycle.selection_generation==toggleGeneration+1&&store.Collection(root).shots.size()==1,
        "actual checkbox orchestration ON binds ready root, increments selection and refreshes without drawing");
    const auto start=STBeginModeCapture(marks,store,mode,shot2,8,0);
    Check(start.rootID==root&&start.targetGeneration==mode.generation&&start.revision==1,
        "shortcut freezes explicit mode owner and content revision");
    STSelectStrokeHit(*circleHit,lifecycle,selected,previewRoot,previewShot);
    Check(start.rootID==root&&mode.root==root,"later mark selection cannot retarget frozen root capture");
    const auto beforeAppend=copies;complete(start,{nextLine,nextLine});
    Check(copies==beforeAppend&&store.MetadataJobs().size()==1&&store.Collection(root).shots.size()==2,
        "explicit ON capture appends same root without implicit clipboard publication");
    Check(*store.ReadReadyAsset(root,false)==firstBytes,"append preserves original image bytes");
    const auto latest=hit(point(60,60));
    Check(latest&&latest->shotID==shot2&&latest->localSlot==0&&latest->globalSlot==3&&
        latest->markID==StableMarkId(root,3)&&latest->number==4,
        "latest local zero resolves true global mark four, never root mark one");
    Check(hit(point(20,30))&&ownStroke(true,point(20,30))&&marks.LiveShots(root).size()==2,
        "continuously admitted first shot and fresh shot retain ink, numbers and hits");
    const auto exactOldHit=*hit(point(20,30));
    STBeginOwnUIPresentation(marks);
    const auto sourceTarget=marks.own_ui_presentation_source();
    Check(sourceTarget.has_value(),"production owned transition binds only admitted source");
    marks.SetWindowObservation({WindowAvailability::kUnavailable,marks.window_observation().generation+1,Now()});
    bound.generation=marks.window_observation().generation+1;bound.observed=Now();
    Check(STPublishOwnUIPresentation(marks,*sourceTarget,bound,{},true,1,1),"production bound completion publishes privately");
    const std::vector<seethis::core::Rect> covered={{15,15,30,30}};
    Check(!STVisibleStrokeHit(store,marks,visible,context.displays,point(20,30),1,covered)&&
          !STOwnStrokeSelectionInput(true,store,marks,visible,context.displays,point(20,30),1,covered)&&
          STVisibleStrokeHit(store,marks,visible,context.displays,point(60,60),1,covered)&&
          STOwnStrokeSelectionInput(true,store,marks,visible,context.displays,point(60,60),1,covered),
        "own panel covered selection/delete/ownership is denied while outside stroke remains interactive");
    const auto unchanged=hit(point(20,30));
    Check(unchanged&&unchanged->rootID==exactOldHit.rootID&&unchanged->shotID==exactOldHit.shotID&&
          unchanged->markID==exactOldHit.markID&&unchanged->localSlot==exactOldHit.localSlot&&
          unchanged->globalSlot==exactOldHit.globalSlot&&unchanged->number==exactOldHit.number&&
          Serialize(*store.Collection(root).shots.front().metadata)==originalMetadata,
        "ON/completion/own UI retain exact original shot, paths, display/unit/scale/x/y, slots, mark and number");
    Check(hit(point(20,30))&&hit(point(60,60))&&marks.LiveShots(root).size()==2,
        "own Inspector/note production focus transition preserves admitted original ink and hits");
    toggle();Check(!mode.enabled()&&toggleRefreshes==2,"actual checkbox OFF refresh preserves admitted presentation during own UI");
    toggle();Check(mode.enabled()&&toggleRefreshes==3,"actual checkbox ON refresh preserves OLD+NEW ink after completion");
    const auto beforePointer=continuity.epoch();
    continuity.Observe(seethis::platform::ViewportInput::kPointer,ownStroke(true,point(60,60)));
    marks.SetViewportContinuity(continuity.epoch(),continuity.healthy());
    Check(continuity.epoch()==beforePointer&&hit(point(60,60)).has_value(),
        "owned eligible stroke pointer preserves lease before AppKit selection consumes hit");
    Check(!ownStroke(false,point(60,60))&&!ownStroke(true,point(95,95)),
        "external or unknown destination and blank geometry receive no input exemption");
    const auto all=store.MarkSnapshots(root);
    const auto rows=seethis::platform::MarksForShot(all,shot2);
    Check(rows.marks.size()==2&&rows.marks[0].number==4&&rows.marks[1].number==5&&
        all.marks[0].note=="original exact note"&&[InspectorMarkRowText(all.marks[0]) containsString:@"original exact note"],
        "carousel model filters whole global-number/note rows and retains original note");
    marks.SetOwnInputFocus(false);observe(context);marks.EndOwnUIPresentationSource();
    Check(hit(point(20,30))&&marks.LiveShots(root).size()==2,"real external return handoff preserves complete original presentation");
    STSelectStrokeHit(*latest,lifecycle,selected,previewRoot,previewShot);
    selected.clear();
    const auto third=STBeginModeCapture(marks,store,mode,shot3,8,0);
    complete(third,{nextLine});
    Check(store.Collection(root).shots.size()==3&&store.MarkSnapshots(root).marks.back().number==6,
        "persistent ON without selected mark advances reference-global numbering");
    Check(mode.enabled()&&marks.LiveShots(root).size()==3,"ON and all continuous admitted shots survive success");
    Check(marks.BeginPresentationPrefix()&&marks.LiveShots(root).size()==3&&marks.marks().size()==3&&
        !hit(point(60,60)),"pending prefix keeps ink/numbers membership but suspends hit");
    Check(marks.ConfirmPresentationPrefix()&&hit(point(60,60)),"exact prefix confirmation resumes only retained hit membership");
    InteractionController gesture;seethis::platform::CaptureShortcutHold physical;
    physical.Begin(0);
    Check(gesture.ShortcutKeyDown(100,point(10,70))==StartResult::kStarted,"actual shortcut class starts one gesture");
    Check(gesture.PointerDown(point(10,70),101)&&gesture.PointerMoved(point(25,70),102)&&gesture.PointerUp(point(30,70),103)&&
        gesture.PointerDown(point(50,80),104)&&gesture.PointerMoved(point(65,80),105)&&gesture.PointerUp(point(70,80),106)&&
        gesture.Snapshot().regions.size()==2&&gesture.Snapshot().state==InteractionState::kDrawing,
        "one held capture accepts two complete strokes before shortcut release");
    const auto fourth=STBeginModeCapture(marks,store,mode,std::string(32,'e'),8,100);
    Check(marks.BindContext(fourth.generation,context),"multi-stroke capture context binds");
    CaptureResult pixels;pixels.requested=Now();pixels.completed=Now();pixels.pixels={100,100,Bytes(40000,255)};
    marks.Captured(fourth.generation,std::move(pixels));
    Check(gesture.ShortcutKeyUp(107),"only shortcut release completes held two-stroke capture");
    marks.Release(fourth.generation,gesture.Snapshot().subpaths,Now(),107);store.WaitForIdleForTesting();
    Check(mode.enabled()&&store.Collection(root).shots.size()==4&&store.MarkSnapshots(root).marks.back().number==8,
        "three separate ON shortcut cycles persist without reselecting owner");
    const auto cancelled=STBeginModeCapture(marks,store,mode,std::string(32,'f'),8,108);
    (void)cancelled;(void)gesture.ShortcutKeyDown(108,point(10,70));
    seethis::platform::CancelCaptureAttempt(gesture,marks,"component context failure");
    Check(!marks.Pending()&&!marks.AllowsDrawing()&&gesture.Snapshot().state==InteractionState::kIdle&&
        mode.enabled()&&marks.LiveShots(root).size()==4&&physical.key().has_value(),
        "coordinated failure clears drawing while retaining valid mode/presentation and held-key block");
    Check(physical.ObserveRelease(false)&&gesture.ShortcutKeyDown(110,point(10,70))==StartResult::kStarted,
        "actual release then fresh shortcut can start after failure");
    (void)gesture.Cancel(CancelReason::kUser);
    auto hidden=visible;hidden.state=GlobalVisibilityState::kInvisible;
    Check(!STVisibleStrokeHit(store,marks,hidden,context.displays,point(60,60),1)&&
        !STOwnStrokeSelectionInput(true,store,marks,hidden,context.displays,point(60,60),1),
        "hidden stroke cannot select or claim input exemption");
    auto topology=context.displays;topology[0].scale=2;
    Check(!STVisibleStrokeHit(store,marks,visible,topology,point(60,60),1),"wrong topology cannot select");
    auto invalid=point(60,60);invalid.backing_scale=2;
    Check(!hit(invalid)&&!hit(point(60,60),-1),"invalid scale/radius cannot select");
    auto otherContext=context;++otherContext.window_id;observe(otherContext);
    Check(!hit(point(60,60)),"different current context denies latest stroke");
    observe(context);Check(hit(point(60,60)).has_value()&&ownStroke(true,point(60,60)),
        "fresh matching source restores saved hit and exact existing input ownership policy");
    // Root-only dropdown selection supplies no mark, retaining ordinary new-root semantics.
    mode.Disable();selected=lineHit->markID;lifecycle.selected_id=root;
    complete(STBeginModeCapture(marks,store,mode,other,8,0),{circle});
    Check(store.Collection(other).shots.size()==1&&store.MetadataJobs().size()==2,
        "OFF starts new root even while a real mark and reference remain selected");
    Check(mode.Enable(root,true),"ready target may be explicitly enabled again");
    const auto pending=STBeginModeCapture(marks,store,mode,std::string(32,'9'),8,0);
    mode.ReferenceSelected(other);
    Check(!mode.enabled()&&pending.rootID==root,"reference switch turns OFF without retargeting pending capture");
    marks.Abort("pending frozen owner cancelled");
    // Repeated references at identical geometry retain newest-reference hit order.
    const auto newest=hit(point(20,30));Check(newest&&newest->rootID==other,"newest reference wins overlap");
    Check(store.Delete(other)==DeleteResult::kDeleted,"deleted root transitions terminal");
    const auto afterDelete=hit(point(20,30));
    Check(!afterDelete||afterDelete->rootID!=other,"deleted root cannot supply a target while another valid saved root may remain");
    std::ofstream corrupt(std::filesystem::path(directory)/(root+".collection"),std::ios::binary);
    corrupt<<"corrupt";corrupt.close();
    Check(store.Collection(root).status==MarkNotesStatus::kCorrupt&&!hit(point(60,60)),
        "corrupt collection cannot supply a target");
  }
  std::filesystem::remove_all(directory);
  std::cout<<"P4 explicit mode/production hit/shortcut/presentation checks passed\n";
}

// Executes the exact transition dispatcher, synchronous observation/scheduling,
// and completion ownership boundary used by STOverlayCoordinator. No NSApp or
// production input adapter is invoked: release/press here is component evidence.
static void TestR1RefreshOrchestration() {
  using namespace seethis::core;
  struct Fixture {
    ReferenceStore store;
    MarkController marks;
    InteractionController gesture;
    seethis::platform::CaptureShortcutHold physical;
    seethis::platform::AppendCaptureMode mode;
    InteractionState previous=InteractionState::kIdle;
    std::uint64_t generation=0;
    std::int64_t clock=1000;
    unsigned begins=0,schedules=0,finishes=0,ids=0;
    bool succeeded=false,selectionPending=true;
    std::string root;
    Context context;
    std::function<void()> observation;
    explicit Fixture(const char* directory):store(directory),marks(store,[](const auto&){return true;}) {
      context.pid=context.window_pid=42;context.self_pid=99;context.window_id=7;
      context.app_name="Fixture";context.bundle_id="test.fixture";context.executable="/fixture";
      context.window_title="Fixture window";context.window={0,0,100,100};context.quartz_to_appkit_top=100;
      context.observed=Now();context.selection_method="frontmost-pid/topmost-normal-containing-initial-point";
      context.excluded_window_ids={8};context.exclusion_method="exact-window-allowlist;self-pid-rejected;child-windows-off";
      context.displays.push_back({"display-a",1,{0,0,100,100},{0,0,100,100},1,{1,-1,0,100}});
      WindowObservation window;window.availability=WindowAvailability::kReady;window.generation=1;
      window.observed=Now();window.pid=42;window.bundle_id=context.bundle_id;window.window_id=7;
      window.bounds=context.window;window.window_title=context.window_title;window.title={1,TitleAvailability::kReady};
      marks.SetWindowObservation(window);marks.SetViewportContinuity(1,true);
    }
    static DisplayPoint Point(double x,double y) {return {1,CoordinateUnit::kLogicalPoints,{x,y},1};}
    static CaptureResult Pixels() {
      CaptureResult result;result.requested=Now();result.completed=Now();
      result.pixels={100,100,Bytes(40000,255)};return result;
    }
    void Abort() {
      seethis::platform::CancelCaptureAttempt(gesture,marks,"injected synchronous source discontinuity");
      previous=gesture.Snapshot().state;
    }
    void Refresh() {
      STRefreshCaptureTransition(gesture,previous,generation,
          [&](const auto& snapshot,std::uint64_t& ownedGeneration) {
        ++begins;std::string operation(32,'0');const char* hex="0123456789abcdef";
        operation[30]=hex[(++ids/16)%16];operation[31]=hex[ids%16];
        const auto start=STBeginModeCapture(marks,store,mode,operation,8,snapshot.key_down_monotonic_ms);
        generation=start.generation;ownedGeneration=generation;root=start.rootID;
        succeeded=false;selectionPending=true;
        Check(generation&&marks.BindContext(generation,context),"R1 production Begin callback binds context");
        gesture.SetSelectedDisplayGeometry({1,{0,0},{100,100},1});
        const auto frozen=generation;
        STObserveAndScheduleCapture(gesture,marks,generation,frozen,[&] {
          if(observation){const auto observe=observation;observe();}
        },[&] {
          ++schedules;
          if(STCompleteCaptureAttempt(marks,generation,frozen,Pixels(),succeeded,selectionPending))Abort();
        });
      },[&](const auto& snapshot) {
        ++finishes;marks.Release(generation,snapshot.subpaths,Now(),snapshot.key_up_monotonic_ms);
      },[&](const auto&) {marks.Abort("component idle transition");});
    }
    bool Press() {
      // Constructed physical sequence; the real input adapter is unchanged.
      if(!physical.Begin(0))return false;
      Check(gesture.ShortcutKeyDown(++clock,Point(10,10))==StartResult::kStarted,"R1 fresh gesture starts");
      Refresh();return gesture.Snapshot().captures_pointer()&&marks.AllowsDrawing();
    }
    void TwoStrokes() {
      const auto before=begins;
      for(unsigned index=0;index<2;++index) {
        const double y=20+index*30;
        Check(gesture.PointerDown(Point(10,y),++clock),"R1 down accepted");Refresh();
        Check(gesture.PointerMoved(Point(30,y),++clock),"R1 drag accepted");
        Check(gesture.PointerUp(Point(40,y),++clock),"R1 native-like up accepted");Refresh();
        Check(gesture.Snapshot().state==InteractionState::kDrawing&&marks.AllowsDrawing(),
            "R1 mouse-up keeps same capture drawing");
      }
      Check(begins==before&&gesture.Snapshot().subpaths.size()==2,
          "R1 same hold has two strokes and no duplicate transaction");
    }
    bool Release(bool modifierFirst=false) {
      const bool finished=modifierFirst?gesture.ShortcutModifierUp(++clock):gesture.ShortcutKeyUp(++clock);
      if(finished)Refresh(); // An ignored Carbon release does not repair the cache.
      Check(physical.ObserveRelease(false),"R1 constructed actual key release clears hold");
      store.WaitForIdleForTesting();return finished;
    }
  };
  for(const bool append:{false,true}) {
    char pattern[]="/tmp/seethis-p4-r1-refresh-XXXXXX";
    const char* directory=mkdtemp(pattern);Check(directory,"R1 owned directory");
    {
      Fixture f(directory);
      if(append) {
        Check(f.Press(),"R1 seed starts through production orchestration");f.TwoStrokes();f.Release();
        Check(f.mode.Enable(f.root,true),"R1 ON binds seed without arming");
      }
      const auto firstBegins=f.begins,firstSchedules=f.schedules;
      const auto readyBefore=f.store.MetadataJobs().size();
      for(unsigned round=0;round<2;++round) {
        Check(f.Press(),append?"R1 ON next round starts":"R1 OFF next round starts");
        f.TwoStrokes();Check(f.Release(round==1),"R1 full release finishes round");
        Check(f.previous==InteractionState::kFinished&&!f.marks.Pending(),"R1 completion cache matches live finished state");
        Check(!append||f.mode.enabled(),"R1 ON survives completion");
      }
      Check(f.begins==firstBegins+2&&f.schedules==firstSchedules+2,
          "R1 each released/fresh round initializes and schedules exactly once");
      Check(append?f.store.Collection(f.root).shots.size()==3:f.store.MetadataJobs().size()==readyBefore+2,
          "R1 ON appends two shots and OFF makes two independent roots");
      const auto beforeAbortBegins=f.begins,beforeAbortSchedules=f.schedules;
      f.observation=[&]{f.Abort();};
      Check(!f.Press(),"R1 synchronous observation abort terminates attempted capture");
      const auto cancelledGeneration=f.generation;
      Check(f.previous==InteractionState::kIdle&&f.gesture.Snapshot().state==InteractionState::kIdle&&
          !f.marks.Pending()&&f.schedules==beforeAbortSchedules&&f.begins==beforeAbortBegins+1,
          "R1 nested cancel cannot commit saved Drawing or schedule pixels");
      Check(!f.Press()&&f.begins==beforeAbortBegins+1,
          "R1 still-held cancelled attempt cannot reopen");
      Check(!f.Release(),"R1 ignored release does not invoke idle Refresh workaround");
      f.observation={};
      Check(f.Press()&&f.begins==beforeAbortBegins+2&&f.schedules==beforeAbortSchedules+1,
          "R1 released next attempt initializes exactly once without an intervening idle refresh");
      const auto newerGeneration=f.generation;
      CaptureResult stale;stale.error="owned stale completion failure";
      Check(!STCompleteCaptureAttempt(f.marks,f.generation,cancelledGeneration,std::move(stale),f.succeeded,f.selectionPending)&&
          f.generation==newerGeneration&&f.succeeded&&f.selectionPending&&f.marks.AllowsDrawing()&&f.gesture.Snapshot().captures_pointer(),
          "R1 actual completion boundary rejects stale failure ownership of newer generation");
      f.TwoStrokes();f.Release();
      // A nested refresh may replace the outer attempt while observation runs.
      // The obsolete outer scheduling and footer must not steal its state.
      const auto oldSchedules=f.schedules;
      f.observation=[&] {
        f.Abort();Check(f.physical.ObserveRelease(false),"R1 nested release is explicit");
        f.observation={};Check(f.Press(),"R1 nested newer generation starts");
      };
      Check(f.Press(),"R1 outer refresh returns with newer attempt still drawing");
      Check(f.schedules==oldSchedules+1&&f.previous==InteractionState::kDrawing,
          "R1 obsolete outer refresh schedules no pixels and preserves newer drawing state");
      f.Abort();f.Release();
    }
    std::filesystem::remove_all(directory);
  }
  std::cout<<"R1 production-shared refresh OFF/ON two-round/two-stroke/abort/generation checks passed\n";
}

// Actual prefix ingress action order + production capture/render composition.
// Native readiness/input/window samples are owned values; no NSApp or posting.
static void TestPrefixCarryPreservation(bool focusOnly=false) {
  using namespace seethis::core;
  using namespace seethis::platform;
  for(bool append:{false,true})for(const auto variant:focusOnly?std::vector<std::string_view>{"focus"}:std::vector<std::string_view>{"confirm","deadline","cursor"}) {
    char pattern[]="/tmp/seethis-prefix-preservation-XXXXXX";
    const char* directory=mkdtemp(pattern);Check(directory,"prefix owned directory");
    {
      ReferenceStore store(directory);MarkController marks(store,[](const auto&){return true;});
      InteractionController gesture;InteractionState previous=InteractionState::kIdle;
      CaptureShortcutHold hold;ViewportContinuity viewport;AppendCaptureMode mode;
      std::uint64_t generation=0;std::int64_t clock=100;bool ownKey=false;
      Context context;context.pid=context.window_pid=42;context.self_pid=99;context.window_id=7;
      context.app_name="Owned fixture";context.bundle_id="test.prefix";context.executable="/owned";
      context.window_title="Unchanged source";context.title={1,TitleAvailability::kReady};
      context.window={0,0,100,100};context.quartz_to_appkit_top=100;context.observed=Now();
      context.native_window={1,700,"prefix-source",1};
      context.selection_method="frontmost-pid/topmost-normal-containing-initial-point";
      context.excluded_window_ids={8};context.exclusion_method="exact-window-allowlist;self-pid-rejected;child-windows-off";
      context.displays.push_back({"display",1,{0,0,100,100},{0,0,100,100},1,{1,-1,0,100}});
      const auto point=[](double x,double y){return DisplayPoint{1,CoordinateUnit::kLogicalPoints,{x,y},1};};
      const auto observe=[&] {
        WindowObservation o;o.availability=WindowAvailability::kReady;o.generation=marks.window_observation().generation+1;
        o.observed=Now();o.pid=context.pid;o.bundle_id=context.bundle_id;o.window_id=context.window_id;
        o.bounds=context.window;o.native_window=context.native_window;o.window_title=context.window_title;o.title=context.title;
        marks.SetWindowObservation(std::move(o));
      };
      const auto input=[&](ViewportInput event) {
        const bool owned=event==ViewportInput::kKeyboard&&STOwnPanelKeyboardInput(ownKey,ownKey,ownKey,false,false);
        STObservePresentationInput(viewport,marks,event,owned,false);
      };
      input(ViewportInput::kHealthy);observe();
      const std::string root(32,'a');std::string operation=root;
      GlobalVisibilitySnapshot shown;shown.state=GlobalVisibilityState::kVisible;
      const auto paint=[&] {
        for(const auto& rendered:marks.marks()) {
          const auto regions=STVisibleReferenceRegions(rendered.value,store.MarkSnapshots(rendered.value.id),shown);
          if(!regions.empty())marks.RecordPresentedSingle(rendered.value,context.displays);
        }
      };
      const auto refresh=[&] {
        STRefreshCaptureTransition(gesture,previous,generation,[&](const auto&,std::uint64_t& ownedGeneration) {
          const auto start=STBeginModeCapture(marks,store,mode,operation,8,clock);
          generation=start.generation;ownedGeneration=generation;
          Check(generation&&marks.BindContext(generation,context),"prefix production Begin/Bind succeeds");
          Check(STObserveAndScheduleCapture(gesture,marks,generation,generation,[&]{observe();},[&] {
            CaptureResult pixels;pixels.requested=Now();pixels.completed=Now();pixels.pixels={100,100,Bytes(40000,255)};
            marks.Captured(generation,std::move(pixels));
          }),"prefix production capture scheduling succeeds");
        },[&](const auto& snapshot){marks.Release(generation,snapshot.subpaths,Now(),clock);},
          [&](const auto&){marks.Abort("owned interaction cancelled");});
      };
      const auto capture=[&](double y) {
        Check(hold.Begin(0)&&gesture.ShortcutKeyDown(++clock,point(20,y))==StartResult::kStarted,
            "fresh physical-hold/controller entry stays legal after provisional prefix ends");refresh();
        for(unsigned stroke=0;stroke<2;++stroke) {
          const double row=y+stroke*8;
          Check(gesture.PointerDown(point(20,row),++clock)&&gesture.PointerMoved(point(40,row),++clock)&&
                gesture.PointerUp(point(50,row),++clock),"prefix capture accepts two distinct subpaths in same held round");
          Check(gesture.Snapshot().captures_pointer(),"stroke mouse-up does not finish capture");
        }
        Check(gesture.ShortcutKeyUp(++clock),"prefix capture completes only at shortcut release");refresh();
        Check(hold.ObserveRelease(false),"physical release clears owned hold");store.WaitForIdleForTesting();
        Check(marks.phase()==ReferencePhase::kCommitted,"prefix screenshot commits atomically");paint();
      };
      const auto fixedScroll=[&] {
        // Native scroll direction is intentionally absent from ViewportInput:
        // both up/down ingress paths deliver kScroll. Exercise repeated events
        // without an own-input exemption or any fresh timestamp manufacture.
        for(unsigned focus=0;focus<3;++focus) {
          observe();const auto fresh=marks.window_observation();
          std::optional<OwnUIPresentationSourceTarget> target;
          if(focus) {
            Check(STOwnPanelKeyboardInput(focus==2,focus==2,focus==2,focus==1,focus==1),
                "fixed scroll fixture positively owns existing note or Inspector key panel");
            STBeginOwnUIPresentation(marks);target=marks.own_ui_presentation_source();Check(target.has_value(),"fixed scroll source query derives from existing valid saved source");
            marks.SetWindowObservation({WindowAvailability::kUnavailable,marks.window_observation().generation+1,Now()});
            auto privateWindow=fresh;privateWindow.generation+=100;privateWindow.observed=Now();
            Check(STPublishOwnUIPresentation(marks,*target,privateWindow,{},true,7,7),"fixed scroll publishes actual fresh private source");
          }
          const auto before=marks.marks();std::vector<Bytes> serialized;
          std::vector<STNoteBadge> badges;
          for(const auto& item:before)serialized.push_back(Serialize(item.value));
          for(const auto& job:store.MetadataJobs()) {
            const auto rootBadges=STPresentedNoteBadges(store,marks,job.id,shown,context.displays);
            badges.insert(badges.end(),rootBadges.begin(),rootBadges.end());
          }
          Check(!before.empty()&&!badges.empty(),"fixed scroll begins with saved ink and numbers");
          const auto verify=[&] {
            const auto after=marks.marks();Check(after.size()==before.size(),
                "SCROLL fixed positions: saved ink remains visible immediately during scroll");
            for(std::size_t i=0;i<after.size();++i)Check(Serialize(after[i].value)==serialized[i],
                "SCROLL fixed positions: exact saved regions/subpaths/display/unit/scale/xy and root/shot IDs never translate");
            std::vector<STNoteBadge> next;
            for(const auto& job:store.MetadataJobs()) {
              const auto rootBadges=STPresentedNoteBadges(store,marks,job.id,shown,context.displays);
              next.insert(next.end(),rootBadges.begin(),rootBadges.end());
            }
            Check(next.size()==badges.size(),"SCROLL fixed positions: number badge count remains exact");
            for(std::size_t i=0;i<next.size();++i) {
              const auto& a=badges[i];const auto& b=next[i];
              Check(a.reference_id==b.reference_id&&a.mark_id==b.mark_id&&a.number==b.number&&
                    a.anchor.display_id==b.anchor.display_id&&a.anchor.unit==b.anchor.unit&&
                    a.anchor.backing_scale==b.anchor.backing_scale&&a.anchor.position.x==b.anchor.position.x&&
                    a.anchor.position.y==b.anchor.position.y&&STNoteBadgeHit(b,a.anchor),
                  "SCROLL fixed positions: original number/owner/badge anchor/hit stay exact");
            }
            const auto hit=STVisibleStrokeHit(store,marks,shown,context.displays,point(30,20),1);
            Check(hit&&hit->rootID==root&&hit->shotID==root&&hit->markID==StableMarkId(root,0)&&
                  hit->globalSlot==0&&hit->localSlot==0&&hit->number==1,
                "SCROLL fixed positions: original stroke hit keeps exact root/shot/mark/global-local slot/number");
          };
          for(unsigned direction=0;direction<4;++direction) {
            const auto epoch=viewport.epoch();STObservePresentationInput(viewport,marks,ViewportInput::kScroll,false,false);
            Check(viewport.epoch()>epoch&&viewport.healthy(),"fixed scroll advances raw input epoch without claiming ownership");
            verify();
            if(target) {
              Check(marks.window_observation().availability==WindowAvailability::kUnavailable&&
                    marks.own_ui_presentation_source()->generation==target->generation,
                  "fixed scroll retains original private source while ordinary foreground is unavailable");
              auto next=fresh;next.generation+=200+direction;next.observed=Now();
              Check(STPublishOwnUIPresentation(marks,*target,next,{},true,7,7),"fixed scroll accepts genuinely fresh private callback despite raw epoch change");
            } else observe();
            verify();
          }
          if(target) {
            auto missing=fresh;missing.generation+=300;missing.observed=Now();missing.availability=WindowAvailability::kUnavailable;
            Check(!STPublishOwnUIPresentation(marks,*target,missing,{},true,7,7)&&marks.marks().empty(),
                "fixed scroll does not manufacture freshness when private source genuinely becomes unavailable");
            marks.SetOwnInputFocus(false);observe();marks.EndOwnUIPresentationSource();verify();
          }
        }
      };
      capture(20);if(focusOnly)fixedScroll();
      const auto original=store.Collection(root).shots.front();const auto originalBytes=Serialize(*original.metadata);
      Check(store.UpdateMarkNote(root,StableMarkId(root,0),"original exact note 世界",0).status==MarkNoteUpdateStatus::kUpdated,
          "prefix original note owner saved");
      const auto originalBadges=STPresentedNoteBadges(store,marks,root,shown,context.displays);
      Check(originalBadges.size()==2,"original ink render composition has both global-number badge owners");
      const auto focusClick=[&] {
        const auto before=viewport.epoch();
        unsigned destinations=0,observations=0;
        const auto classify=[&](ViewportInput event,const std::optional<DisplayPoint>& where,
            bool healthy,std::uint64_t destination,const WindowObservation& fresh,
            const std::vector<std::uint64_t>& passThrough=std::vector<std::uint64_t>{8}) {
          return STSourceFocusPreservesPresentation(event,where,healthy,store,marks,shown,context.displays,
              passThrough,[&](std::uint64_t below) {++destinations;return below==0?std::uint64_t{8}:destination;},
              [&](const Context&) {++observations;return fresh;});
        };
        const auto fresh=marks.window_observation();const auto blank=point(90,90);
        auto chrome=context;chrome.bundle_id="com.google.Chrome";
        Check(STSourceFocusPermissionCurrent(context,1,2)&&!STSourceFocusPermissionCurrent(chrome,1,2)&&
              STSourceFocusPermissionCurrent(chrome,2,2),"F3 unrelated Chrome auth generation cannot block nonChrome source; Chrome remains generation-guarded");
        Check(classify(ViewportInput::kPrimaryDown,blank,true,context.window_id,fresh),
            "F3 exact existing source below pass-through panel qualifies before any refresh");
        const auto noCalls=destinations;
        Check(!classify(ViewportInput::kPointer,blank,true,context.window_id,fresh)&&
              !classify(ViewportInput::kScroll,blank,true,context.window_id,fresh)&&
              !classify(ViewportInput::kKeyboard,blank,true,context.window_id,fresh)&&
              !classify(ViewportInput::kPrimaryDown,{},true,context.window_id,fresh)&&
              !classify(ViewportInput::kPrimaryDown,blank,false,context.window_id,fresh)&&destinations==noCalls,
            "F3 drag/right/generic/scroll/keyboard/unknown/unhealthy cannot query or earn source exemption");
        const auto sourceCalls=observations;
        Check(!classify(ViewportInput::kPrimaryDown,blank,true,555,fresh)&&
              !classify(ViewportInput::kPrimaryDown,blank,true,0,fresh)&&
              !classify(ViewportInput::kPrimaryDown,blank,true,context.window_id,fresh,{})&&observations==sourceCalls,
            "F3 foreign occluder/unknown/accepting overlay cannot be skipped or obtain source evidence");
        auto mismatch=fresh;mismatch.window_id++;
        Check(!classify(ViewportInput::kPrimaryDown,blank,true,context.window_id,mismatch),"F3 same-app different window rejected");
        mismatch=fresh;mismatch.native_window.incarnation++;
        Check(!classify(ViewportInput::kPrimaryDown,blank,true,context.window_id,mismatch),"F3 reused native window lifetime rejected");
        mismatch=fresh;mismatch.window_title="changed";
        Check(!classify(ViewportInput::kPrimaryDown,blank,true,context.window_id,mismatch),"F3 title change rejected");
        mismatch=fresh;mismatch.bounds.x++;
        Check(!classify(ViewportInput::kPrimaryDown,blank,true,context.window_id,mismatch),"F3 moved source bounds rejected");
        mismatch=fresh;mismatch.observed.monotonic_us-=2'000'001;
        Check(!classify(ViewportInput::kPrimaryDown,blank,true,context.window_id,mismatch),"F3 expired fresh query cannot qualify");
        mismatch=fresh;mismatch.availability=WindowAvailability::kUnavailable;
        Check(!classify(ViewportInput::kPrimaryDown,blank,true,context.window_id,mismatch),"F3 denied or missing source cannot qualify");
        auto wrongDisplays=context.displays;wrongDisplays.front().scale=2;
        Check(!STSourceFocusPreservesPresentation(ViewportInput::kPrimaryDown,blank,true,store,marks,shown,wrongDisplays,
            {},[](std::uint64_t){return std::uint64_t{7};},[&](const Context&){return fresh;}),"F3 display topology/unit/scale mismatch rejected");
        const bool preserves=classify(ViewportInput::kPrimaryDown,blank,true,context.window_id,fresh);
        // Real coordinator has the same separate continuity flag; own/ownFocus
        // and all pointer acceptance/hit paths are unaffected by this decision.
        STObservePresentationInput(viewport,marks,ViewportInput::kPrimaryDown,false,preserves);
        Check(viewport.epoch()==before&&viewport.healthy(),
            "F3 same-source primary down preserves first-single and multishot continuity epoch");
      };
      if(focusOnly)focusClick();
      InspectorLifecycleState lifecycle;lifecycle.selected_id=root;
      if(append)STToggleInspectorAppendMode(mode,store,lifecycle,[&](NSString* status) {
        Check(!status&&!STValidateAppendMode(mode,store,marks),"production append mode and refresh validation bind root");
      });
      unsigned cancelled=0;
      CapturePrefixIngress ingress;
      // Same order as MacInputAdapter's constructor; actual native callback
      // sensitivity is additionally verified with that adapter in owned scratch.
      ingress.SetActions({[&]{return marks.BeginPresentationPrefix();},[&]{(void)marks.ConfirmPresentationPrefix();},
        [&]{++cancelled;marks.CancelPresentationPrefix();},[&]{input(ViewportInput::kInterrupted);},[]{}});
      CapturePrefixReadiness ready;ready.running=ready.settings_usable=ready.registered=ready.handler_ready=true;
      ready.monitor_ready=ready.screen_ready=ready.shortcut_ready=ready.native_binding_matches=true;
      ready.configured_key=ready.registered_key=0;ready.configured_modifiers=ready.registered_modifiers=kShortcutCommand;
      ready.registration_generation=1;const std::uint64_t modifiers=1ull<<20;
      for(unsigned round=1;round<=2;++round) {
        if(focusOnly)focusClick();
        ingress.Released(0);const auto tick=1000*round;const auto epoch=viewport.epoch();
        Check(ingress.ModifierEvent(ready,modifiers,modifiers,tick),"production ingress starts provisional prefix");
        if(std::string_view(variant)=="deadline")ingress.Tick(tick+500);
        else if(std::string_view(variant)=="cursor")ingress.Invalidate();
        else ingress.Confirm(ready,tick+100);
        Check(viewport.epoch()==epoch&&viewport.healthy(),"benign deadline/cursor introduces no viewport change");
        ingress.Confirm(ready,tick+600);clock=tick+600;operation=std::string(32,static_cast<char>('a'+round));
        capture(45+round*15);
        if(focusOnly)fixedScroll();
        const auto collection=store.Collection(root);const auto notes=store.MarkSnapshots(root);
        Check(Serialize(*collection.shots.front().metadata)==originalBytes&&collection.shots.front().id==root&&
              notes.marks[0].id==StableMarkId(root,0)&&notes.marks[0].number==1&&notes.marks[0].slot==0&&
              notes.marks[0].shot_id==root&&notes.marks[0].local_slot==0&&notes.marks[0].note=="original exact note 世界",
            "prefix rounds retain exact original serialized regions/subpaths/display-unit-scale-xy and number/note/shot identity");
        const auto oldHit=STVisibleStrokeHit(store,marks,shown,context.displays,point(30,20),1);
        Check(oldHit&&oldHit->rootID==root&&oldHit->shotID==root&&oldHit->markID==StableMarkId(root,0)&&
              oldHit->localSlot==0&&oldHit->globalSlot==0&&oldHit->number==1,
            "prefix preservation keeps original live mark #1 with exact root/shot/global-local ownership");
        const auto newHit=STVisibleStrokeHit(store,marks,shown,context.displays,point(30,45+round*15),1);
        const auto expectedRoot=append?root:operation;const auto slot=append?round*2:0;
        Check(newHit&&newHit->rootID==expectedRoot&&newHit->shotID==operation&&newHit->globalSlot==slot&&
              newHit->localSlot==0&&newHit->markID==StableMarkId(expectedRoot,slot)&&newHit->number==slot+1,
            "prefix ON/OFF new shot retains its actual root, shot and global/local numbering");
        Check(collection.shots.size()==(append?round+1:1)&&store.MetadataJobs().size()==(append?1:round+1),
            "prefix ON appends same root while OFF creates separate roots each round");
        const auto badges=STPresentedNoteBadges(store,marks,root,shown,context.displays);
        Check(badges.size()>1,"prefix keeps original number badges live after each round");
        for(unsigned index=0;index<2;++index) {
          const auto& before=originalBadges[index];const auto& after=badges[index];
          Check(before.reference_id==after.reference_id&&before.mark_id==after.mark_id&&before.number==after.number&&
                before.anchor.display_id==after.anchor.display_id&&before.anchor.unit==after.anchor.unit&&
                before.anchor.backing_scale==after.anchor.backing_scale&&before.anchor.position.x==after.anchor.position.x&&
                before.anchor.position.y==after.anchor.position.y&&STNoteBadgeHit(after,before.anchor),
              "prefix original badge anchor/display/unit/scale/xy/number/note owner and hit stay immutable");
        }
      }
      Check(cancelled==((std::string_view(variant)=="confirm"||focusOnly)?0:2),"actual ingress invokes benign cancellation each intended round");
      // Actual readiness loss must not be mistaken for owned note typing.
      if(append) {
        ownKey=true;STBeginOwnUIPresentation(marks);ingress.Released(0);
        if(focusOnly) {
          const auto privateTarget=marks.own_ui_presentation_source();Check(privateTarget.has_value(),"F3 existing own UI source derives from actual admitted/presented capture");
          const auto external=marks.window_observation();auto unavailable=external;
          unavailable.generation++;unavailable.availability=WindowAvailability::kUnavailable;marks.SetWindowObservation(unavailable);
          unsigned queries=0;
          Check(STSourceFocusPreservesPresentation(ViewportInput::kPrimaryDown,point(90,90),true,store,marks,shown,context.displays,{},
              [](std::uint64_t){return std::uint64_t{7};},[&](const Context&){++queries;return external;})&&queries>0&&
              marks.own_ui_presentation_source()->generation==privateTarget->generation,
              "F3 fresh existing private membership qualifies without creating/replacing target or observations");
          marks.SetOwnInputFocus(false);observe();
        }
        Check(ingress.ModifierEvent(ready,modifiers,modifiers,4000),"owned key panel readiness prefix starts");
        auto unavailable=ready;unavailable.monitor_ready=false;
        Check(!ingress.Readiness(unavailable)&&!viewport.healthy()&&marks.LiveShots(root).empty(),
            "readiness interruption retires immediately despite positively owned key panel");
        const auto stored=store.Collection(root).shots.size();input(ViewportInput::kHealthy);observe();
        Check(marks.LiveShots(root).size()==stored&&store.Collection(root).shots.size()==stored&&!marks.Pending(),
            "genuine Healthy plus fresh source restores saved projection without starting capture");
        marks.SetOwnInputFocus(false);ownKey=false;operation=std::string(32,'f');clock=5000;capture(40);
        const auto live=marks.LiveShots(root);
        Check(live.size()==stored+1&&live.back().id==operation&&marks.IsShotAdmitted(root,root),
            "new capture adds geometry without erasing recovered original saved shots");
      }
      if(focusOnly) {
        // Scroll changes raw capture continuity while every saved screen
        // coordinate remains fixed. It never claims input ownership.
        for(unsigned repeat=0;repeat<4;++repeat) {
          const auto before=viewport.epoch();input(ViewportInput::kScroll);
          Check(viewport.epoch()>before&&marks.LiveShots(root).size()==store.Collection(root).shots.size(),
              "SCROLL fixed positions: raw scroll preserves all saved shots before observation refresh");
          observe();
          Check(STVisibleStrokeHit(store,marks,shown,context.displays,point(30,20),1).has_value(),
              "SCROLL fixed positions: refresh retains original screen hit");
        }
        auto moved=marks.window_observation();moved.generation++;moved.observed=Now();moved.bounds.x++;marks.SetWindowObservation(moved);
        Check(marks.LiveShots(root).empty(),"current different window geometry temporarily hides saved shots");
        observe();
        unsigned sourceReads=0;
        const auto allowed=STSourceFocusPreservesPresentation(ViewportInput::kPrimaryDown,point(90,90),true,store,marks,shown,context.displays,{},
            [](std::uint64_t){return std::uint64_t{7};},[&](const Context&){++sourceReads;return marks.window_observation();});
        Check(marks.LiveShots(root).size()==store.Collection(root).shots.size(),"fresh original window geometry restores all matching saved shots");
        // Single ordinary eligibility can remain visible by the pre-existing
        // window policy; classification never recreates its retired carry.
        if(append)Check(allowed&&sourceReads>0,"F3 fresh matching saved projection stays independently eligible after geometry return");
        Check(!marks.own_ui_presentation_source(),"F3 source click after window movement never constructs an own UI target");
        if(!append) {
          Check(mode.Enable(root,true),"F3 deliberate new append mode after retirement");operation=std::string(32,'d');clock=6000;capture(10);
          const auto after=marks.LiveShots(root);
          Check(after.size()==2&&after.back().id==operation&&marks.IsShotAdmitted(root,root),
              "F3 recovered saved single and new append retain their actual stored ownership");
        }
        auto expired=marks.window_observation();expired.generation++;expired.observed.monotonic_us-=2'000'001;
        marks.SetWindowObservation(expired);unsigned reads=0;
        Check(!STSourceFocusPreservesPresentation(ViewportInput::kPrimaryDown,point(90,90),true,store,marks,shown,context.displays,{},
            [](std::uint64_t){return std::uint64_t{7};},[&](const Context&){++reads;return marks.window_observation();})&&reads==0,
            "F3 current eligibility must be fresh BEFORE any matching query can run");
        observe();Check(marks.LiveShots(root).size()==store.Collection(root).shots.size(),"fresh source restores temporary age and geometry loss for all saved members");
      }
    }
    std::filesystem::remove_all(directory);
  }
  std::cout<<"Prefix ON/OFF two-round multi-stroke confirm/deadline/cursor exact identity and readiness recovery checks passed\n";
}


static void TestPendingSourceScroll() {
  using namespace seethis::core;using namespace seethis::platform;
  // First source, separate new source, append from a new source, and Ready
  // before queued completion all retain the exact captured screen coordinates.
  for(int mode=0;mode<4;++mode) {
    char pattern[]="/tmp/seethis-pending-fixed-scroll-XXXXXX";
    const char* directory=mkdtemp(pattern);Check(directory,"pending scroll owned directory exists");
    {
      ReferenceStore store(directory);std::mutex mutex;std::vector<std::function<void()>> callbacks;
      MarkController marks(store,MarkController::PendingClipboard{},MarkController::ReadyClipboard{},
          [&](std::function<void()> callback){if(mode==3){std::lock_guard lock(mutex);callbacks.push_back(std::move(callback));}else callback();});
      const auto dispatch=[&] {std::vector<std::function<void()>> batch;{std::lock_guard lock(mutex);batch.swap(callbacks);}for(auto& callback:batch)callback();};
      ViewportContinuity viewport;STObservePresentationInput(viewport,marks,ViewportInput::kHealthy,false,false);
      Context context;context.pid=context.window_pid=42;context.self_pid=99;context.window_id=7;
      context.app_name="Owned fixture";context.bundle_id="test.owned.pending";context.executable="/owned";
      context.window_title="Exact pending source";context.title={1,TitleAvailability::kReady};context.window={0,0,100,100};
      context.quartz_to_appkit_top=100;context.observed=Now();context.native_window={1,700,"owned-pending",1};
      context.selection_method="frontmost-pid/topmost-normal-containing-initial-point";
      context.excluded_window_ids={8};context.exclusion_method="exact-window-allowlist;self-pid-rejected;child-windows-off";
      context.displays.push_back({"display",1,{0,0,100,100},{0,0,100,100},1,{1,-1,0,100}});
      const auto observe=[&] {
        WindowObservation value;value.availability=WindowAvailability::kReady;value.generation=marks.window_observation().generation+1;
        value.observed=Now();value.pid=context.window_pid;value.bundle_id=context.bundle_id;value.window_id=context.window_id;
        value.bounds=context.window;value.native_window=context.native_window;value.window_title=context.window_title;value.title=context.title;marks.SetWindowObservation(value);
      };
      const auto point=[](double x,double y){return DisplayPoint{1,CoordinateUnit::kLogicalPoints,{x,y},1};};
      const std::vector<DisplayPoint> path{point(20,20),point(40,40)};
      const auto pixels=[] {CaptureResult value;value.requested=Now();value.completed=Now();value.pixels={100,100,Bytes(40000,255)};return value;};
      const std::string root(32,'a'),next(32,'b');
      if(mode) {
        observe();const auto first=marks.Begin(root);Check(marks.BindContext(first,context),"pending initial context binds");marks.Captured(first,pixels());
        marks.Release(first,path,Now());store.WaitForIdleForTesting();dispatch();
        Check(store.Collection(root).shots.size()==1,"pending initial shot persists");
        if(mode<3){context.window_id=9;context.window_title="Different pending source";context.observed=Now();}
      }
      observe();const auto generation=mode>=2?marks.BeginAppendReference(root,store.Collection(root).revision,next):marks.Begin(mode?next:root);
      Check(generation&&marks.BindContext(generation,context),"pending fixed scroll ordinary capture binds");marks.Release(generation,path,Now());
      for(unsigned repeat=0;repeat<4;++repeat) {
        const auto epoch=viewport.epoch();STObservePresentationInput(viewport,marks,ViewportInput::kScroll,false,false);
        Check(viewport.epoch()>epoch&&marks.phase()==ReferencePhase::kReleasedPendingCapture,"pending scroll advances raw epoch while accepted capture still waits for pixels");
      }
      auto geometry=marks.window_observation();geometry.generation++;geometry.observed=Now();geometry.bounds.width++;
      marks.SetWindowObservation(geometry);
      auto changedDisplays=context.displays;changedDisplays[0].logical.x++;changedDisplays[0].logical_to_pixels.tx--;
      marks.SetDisplayObservation(changedDisplays);
      marks.Captured(generation,pixels());store.WaitForIdleForTesting();
      Check(marks.marks().empty(),"pending geometry/topology mismatch hides current saved projection without permanent poison");
      marks.SetDisplayObservation(context.displays);observe();
      const auto storedRoot=mode==1?next:root;const auto shot=mode?next:root;
      const auto collection=store.Collection(storedRoot);
      Check(collection.shots.size()==(mode>=2?2:1),"pending scroll atomically persists complete shot BEFORE queued callback");
      if(mode==3){std::lock_guard lock(mutex);Check(callbacks.size()==1,"pending scroll exact completion remains queued after Ready publication");}
      const auto visible=[&] {const auto live=marks.LiveShots(storedRoot);return std::any_of(live.begin(),live.end(),[&](const auto& value){return value.id==shot;});};
      Check(visible(),"SCROLL fixed positions: pending captured geometry remains visible before queued completion");
      geometry=marks.window_observation();geometry.generation++;geometry.observed=Now();geometry.bounds.width++;
      marks.SetWindowObservation(geometry);Check(!visible(),"Ready-before-queued current geometry mismatch still hides");
      observe();Check(visible(),"Ready-before-queued exact original return restores pending published coordinates");
      STObservePresentationInput(viewport,marks,ViewportInput::kScroll,false,false);observe();Check(visible(),"pending fresh return never creates scroll exclusion");
      dispatch();Check(marks.phase()==ReferencePhase::kCommitted&&visible(),"pending scroll completion preserves saved old/new coordinates");
      const auto& saved=*collection.shots.back().metadata;const auto savedPath=EffectiveRegions(saved).front().path;
      Check(saved.context.window_id==context.window_id&&saved.id==storedRoot&&saved.mark_id==storedRoot&&collection.shots.back().id==shot&&savedPath.size()==path.size(),
          "pending scroll preserves persisted root/shot identity and path size");
      for(std::size_t i=0;i<path.size();++i)Check(savedPath[i].display_id==path[i].display_id&&savedPath[i].unit==path[i].unit&&
          savedPath[i].backing_scale==path[i].backing_scale&&savedPath[i].position.x==path[i].position.x&&savedPath[i].position.y==path[i].position.y,
          "pending scroll preserves exact persisted display/unit/scale/coordinates");
    }
    std::filesystem::remove_all(directory);
  }
}

static void TestRecoverableGeometryHistory(bool nativeReplacement) {
  const int guard=nativeReplacement?2:0;
  using namespace seethis::core;using namespace seethis::platform;
  char pattern[]="/tmp/seethis-owned-aba-XXXXXX";const char* directory=mkdtemp(pattern);Check(directory,"owned ABA directory");
  {
    ReferenceStore store(directory);MarkController marks(store,[](const auto&){return true;});
    ViewportContinuity viewport;STObservePresentationInput(viewport,marks,ViewportInput::kHealthy,false,false);
    Context context;context.pid=context.window_pid=42;context.self_pid=99;context.window_id=7;
    context.app_name="Owned conversation fixture";context.bundle_id="test.owned.conversation";context.executable="/owned";
    context.window_title="Owned title A";context.title={1,TitleAvailability::kReady};context.window={0,0,100,100};
    context.quartz_to_appkit_top=100;context.observed=Now();context.native_window={1,700,"owned-aba-session",1};
    context.selection_method="frontmost-pid/topmost-normal-containing-initial-point";
    context.excluded_window_ids={8};context.exclusion_method="exact-window-allowlist;self-pid-rejected;child-windows-off";
    context.displays.push_back({"owned-display",1,{0,0,100,100},{0,0,100,100},1,{1,-1,0,100}});
    const auto observe=[&] {
      WindowObservation value;value.availability=WindowAvailability::kReady;value.generation=marks.window_observation().generation+1;
      value.observed=Now();value.pid=context.window_pid;value.bundle_id=context.bundle_id;value.window_id=context.window_id;
      value.bounds=context.window;value.native_window=context.native_window;value.window_title=context.window_title;value.title=context.title;
      marks.SetWindowObservation(value);marks.SetDisplayObservation(context.displays);
    };
    const auto point=[](unsigned number){return DisplayPoint{1,CoordinateUnit::kLogicalPoints,{30,10+number*9.0},1};};
    const std::string root(32,'a');AppendCaptureMode mode;
    GlobalVisibilitySnapshot shown;shown.state=GlobalVisibilityState::kVisible;
    std::vector<Bytes> immutable;std::vector<unsigned> shotForMark,localForMark;unsigned total=0;
    const auto states=[&](const char* stage) {
      const auto collection=store.Collection(root);std::cout<<"STATE guard="<<guard<<" stage="<<stage<<" revision="<<collection.revision;
      for(std::size_t i=0;i<collection.shots.size();++i)std::cout<<" shot"<<i+1<<'='<<static_cast<int>(marks.SavedProjection(*collection.shots[i].metadata));
      std::cout<<'\n';
    };
    const auto verify=[&](const std::vector<unsigned>& expected,const char* stage) {
      const auto collection=store.Collection(root);const auto notes=store.MarkSnapshots(root);const auto live=marks.LiveShots(root);
      Check(collection.status==MarkNotesStatus::kReady&&notes.status==MarkNotesStatus::kReady,"owned ABA snapshots Ready");
      std::set<std::string> expectedShots;for(const auto number:expected)expectedShots.insert(collection.shots[shotForMark[number-1]].id);
      Check(live.size()==expectedShots.size()&&marks.marks().size()==expectedShots.size(),"owned ABA desktop projects ALL matching per-shot members");
      unsigned painted=0;
      for(const auto& rendered:marks.marks()) {
        const auto visible=STVisibleReferenceRegions(rendered.value,notes,shown);painted+=visible.size();
        const auto found=std::find_if(collection.shots.begin(),collection.shots.end(),[&](const auto& shot){return shot.first_mark_slot==rendered.value.first_mark_slot;});
        Check(found!=collection.shots.end()&&expectedShots.contains(found->id)&&Serialize(rendered.value)==Serialize(*found->metadata),
            "owned ABA actual desktop render uses exact immutable per-shot source/regions/subpaths");
      }
      Check(painted==expected.size(),"owned ABA production render-region count equals exact eligible numbers");
      const auto badges=STPresentedNoteBadges(store,marks,root,shown,context.displays);std::vector<unsigned> actual;
      for(const auto& badge:badges)actual.push_back(badge.number);
      Check(actual==expected,"owned ABA production number badges show exact A/B eligible set");
      for(unsigned number=1;number<=total;++number) {
        const auto& note=notes.marks[number-1];const auto shot=shotForMark[number-1];const auto local=localForMark[number-1];
        Check(note.id==StableMarkId(root,number-1)&&note.slot==number-1&&note.number==number&&
              note.shot_id==collection.shots[shot].id&&note.local_slot==local&&note.note=="owned exact note "+std::to_string(number),
            "owned ABA immutable global/local number/slot/shot/note owner remains exact");
        const auto hit=STVisibleStrokeHit(store,marks,shown,context.displays,point(number),1);
        const bool visible=std::find(expected.begin(),expected.end(),number)!=expected.end();
        if(visible) {
          Check(hit&&hit->rootID==root&&hit->shotID==note.shot_id&&hit->markID==note.id&&hit->globalSlot==note.slot&&hit->localSlot==local&&hit->number==number,
              "owned ABA actual production stroke hit keeps exact original number/root/shot/owner");
          const auto badge=std::find_if(badges.begin(),badges.end(),[&](const auto& value){return value.number==number;});
          Check(badge!=badges.end()&&badge->reference_id==root&&badge->mark_id==note.id&&STNoteBadgeHit(*badge,badge->anchor),
              "owned ABA original badge hit and owner remain exact");
        } else Check(!hit,"owned ABA other-title/guard-excluded stroke has no desktop hit");
      }
      for(std::size_t shot=0;shot<immutable.size();++shot)Check(Serialize(*collection.shots[shot].metadata)==immutable[shot],
          "owned ABA old exact serialized geometry/native/display/unit/scale/coordinates never rewritten");
      // This is the exact production preview filter used by changeInspectorShot.
      // Cycling every shot (including hidden B) must not filter desktop members.
      for(const auto& shot:collection.shots) {
        const auto preview=MarksForShot(notes,shot.id);Check(preview.marks.size()==EffectiveRegions(*shot.metadata).size(),"owned ABA carousel filters only selected shot rows");
        Check(marks.LiveShots(root).size()==expectedShots.size()&&STPresentedNoteBadges(store,marks,root,shown,context.displays).size()==expected.size(),
            "owned ABA carousel preview never grants or filters desktop projection");
      }
      std::cout<<"ABA guard="<<guard<<" stage="<<stage<<" shots="<<collection.shots.size()<<" numbers=";for(auto n:actual)std::cout<<n<<',';std::cout<<'\n';
    };
    const auto temporaryGeometry=[&] {
      const auto before=store.Collection(root);std::vector<SavedProjectionState> states;
      for(const auto& shot:before.shots)states.push_back(marks.SavedProjection(*shot.metadata));
      auto moved=marks.window_observation();moved.generation++;moved.observed=Now();moved.bounds.width++;
      marks.SetWindowObservation(moved);
      Check(marks.marks().empty(),"GEOMETRY recovery: current different bounds hide all old saved ink");
      auto changed=context.displays;changed[0].logical.x++;changed[0].logical_to_pixels.tx--;
      marks.SetDisplayObservation(changed);observe();
      // observe also publishes original topology. Test topology independently.
      marks.SetDisplayObservation(changed);Check(marks.marks().empty(),"GEOMETRY recovery: current valid different topology hides old saved ink");
      marks.SetDisplayObservation(context.displays);observe();
      for(std::size_t i=0;i<before.shots.size();++i)Check(marks.SavedProjection(*before.shots[i].metadata)==states[i],
          "GEOMETRY recovery: fresh exact original observation restores all saved A shot states before append");
    };
    for(unsigned shot=0;shot<8;++shot) {
      if(shot==3)temporaryGeometry();
      if(shot==7) {
        context.window_title="Owned title B";observe();
        for(unsigned scroll=0;scroll<4;++scroll)STObservePresentationInput(viewport,marks,ViewportInput::kScroll,false,false);
        std::this_thread::sleep_for(std::chrono::milliseconds(2050));
        Check(marks.marks().empty(),"repeated history long gap hides stale current source without new capture");
        context.window_title="Owned title A";context.observed=Now();observe();temporaryGeometry();
        verify(nativeReplacement?std::vector<unsigned>{7,8}:std::vector<unsigned>{1,2,3,4,7,8},"A-second-return-before9");
      }
      if(shot==3) {
        context.window_title="Owned title B";
        if(guard==2)++context.native_window.incarnation;
      }
      if(shot==5) {context.window_title="Owned title A";}
      states("before-source-observation");context.observed=Now();observe();states("after-source-observation");
      for(unsigned repeat=0;repeat<2;++repeat)STObservePresentationInput(viewport,marks,ViewportInput::kScroll,false,false);
      const std::string operation(32,shot>=6?static_cast<char>('1'+shot-6):static_cast<char>('a'+shot));const auto start=STBeginModeCapture(marks,store,mode,operation,8,100+shot);
      Check(start.generation&&marks.BindContext(start.generation,context),"owned ABA actual Begin/Bind binds ordinary source");
      CaptureResult pixels;pixels.requested=Now();pixels.completed=Now();pixels.pixels={100,100,Bytes(40000,static_cast<unsigned char>(200+shot))};
      marks.Captured(start.generation,pixels);
      std::vector<std::vector<DisplayPoint>> strokes;
      for(unsigned local=0;local<(shot==1?2:1);++local) {
        ++total;auto left=point(total),right=left;left.position.x=20;right.position.x=40;
        strokes.push_back({left,right});shotForMark.push_back(shot);localForMark.push_back(local);
      }
      marks.Release(start.generation,strokes,Now(),200+shot);store.WaitForIdleForTesting();
      Check(marks.phase()==ReferencePhase::kCommitted,"owned ABA real atomic append callback completes");states("after-atomic-completion");
      const auto collection=store.Collection(root);Check(collection.shots.size()==shot+1&&collection.revision==shot+1,"owned ABA exact atomic shot/revision order");
      immutable.push_back(Serialize(*collection.shots.back().metadata));
      auto notes=store.MarkSnapshots(root);
      for(unsigned number=total-strokes.size()+1;number<=total;++number) {
        Check(store.UpdateMarkNote(root,StableMarkId(root,number-1),"owned exact note "+std::to_string(number),notes.revision).status==MarkNoteUpdateStatus::kUpdated,
            "owned ABA note has exact immutable original owner");notes=store.MarkSnapshots(root);
      }
      if(shot==0)Check(mode.Enable(root,true),"owned ABA append mode binds existing ready root");
      observe();
      if(shot==2)verify({1,2,3,4},"A-first-three");
      if(shot==4)verify({5,6},"B-two");
      if(shot==6)verify(nativeReplacement?std::vector<unsigned>{7,8}:std::vector<unsigned>{1,2,3,4,7,8},"A-return-two");
      if(shot==7)verify(nativeReplacement?std::vector<unsigned>{7,8,9}:std::vector<unsigned>{1,2,3,4,7,8,9},"A-final-nine");
    }
  }
  std::filesystem::remove_all(directory);
}

int main(int argc,char** argv) {
  TestRecoverableGeometryHistory(false);TestRecoverableGeometryHistory(true);
  if(argc==2&&std::string_view(argv[1])=="--recoverable-history-component-only")return 0;
  TestPendingSourceScroll();
  if(argc==2&&std::string_view(argv[1])=="--pending-source-scroll-component-only")return 0;
  TestPrefixCarryPreservation(true);
  if(argc==2&&std::string_view(argv[1])=="--focus-presentation-component-only")return 0;
  TestPrefixCarryPreservation();
  if(argc==2&&std::string_view(argv[1])=="--prefix-carry-component-only")return 0;
  TestR1RefreshOrchestration();TestStrokeSelectionCaptureFlow();
  if(argc==2&&std::string_view(argv[1])=="--p4-component-only") {
    Check(InspectorMarkAction(1)==STInspectorMarkAction::kSelect,"single row click only selects mark; explicit mode controls capture ownership");
    Check(InspectorMarkAction(2)==STInspectorMarkAction::kOpenNote,"explicit double click opens note without a live overlay");
    using namespace seethis::core;
    MarkNotesSnapshot all;all.status=MarkNotesStatus::kReady;all.revision=7;
    const std::string root(32,'a'),a(32,'b'),b(32,'c');
    MarkSnapshot one;one.id=StableMarkId(root,0);one.slot=0;one.number=1;one.note="first exact";one.shot_id=a;
    MarkSnapshot two;two.id=StableMarkId(root,1);two.slot=1;two.number=2;two.note="second exact";two.shot_id=b;
    all.marks={one,two};
    const auto first=seethis::platform::MarksForShot(all,a),second=seethis::platform::MarksForShot(all,b);
    Check(first.marks.size()==1&&first.marks[0].number==1&&first.marks[0].note=="first exact","first shot filters complete row");
    Check(second.marks.size()==1&&second.marks[0].number==2&&second.marks[0].id==two.id&&second.marks[0].note=="second exact","second shot restores original global number and note");
    Check(seethis::platform::MarksForShot(all,"empty").marks.empty(),"empty shot displays empty rows");
    Check(seethis::platform::MarksForShot(all,a).marks[0].id==one.id,"returning shot restores identity unchanged");
    Check(!InspectorPreviewCompletionMatches(false,true,root,root,"digest",@"digest",@"key",@"key"),"late thumbnail cannot steal newer explicit selection");
    Check(!InspectorPreviewCompletionMatches(true,true,root,root,"new",@"old",@"key",@"key"),"digest mismatch cannot attach old thumbnail");
    std::cout<<"P4 carousel pure component checks passed\n";return 0;
  }
  @autoreleasepool {
    TestCapturedReferenceLabels();
    TestPersistedCapturedLabel();
    TestPreviewNumbers();
    TestSourcePngPreview();
    MarkSnapshot mark;
    mark.number=7;
    mark.note="";
    Check([InspectorMarkRowText(mark) isEqualToString:@"7"] &&
          [InspectorSelectedMarkText(mark) isEqualToString:
          @"Selected reference\nMark 7\nNote:\n"],
          "empty persisted note and stable number display exactly in row and detail");
    NSArray<NSString*>* shortRows=@[InspectorMarkRowText(mark),
        @"8  Next",@"9  Third",@"10  Fourth"];
    const auto compact=InspectorBodyLayout(470,270,42,shortRows,YES);
    Check(NSWidth(compact.previewFrame)==438 &&
          NSHeight(compact.previewFrame)==180 &&
          NSMaxY(compact.previewFrame)<=270 &&
          compact.documentHeight>270 &&
          compact.documentHeight>=compact.textTop+42+20,
          "ordinary inspector viewport shows the full preview and scrolls to marks and text");
    const NSRect firstRow=compact.markRowFrames.front();
    const NSRect lastRow=compact.markRowFrames.back();
    Check(InspectorMarkRowAtPoint(NSMakePoint(30,34),compact)==NSNotFound &&
          InspectorMarkRowAtPoint(NSMakePoint(30,NSMinY(firstRow)),compact)==0 &&
          InspectorMarkRowAtPoint(NSMakePoint(30,NSMaxY(lastRow)-1),compact)==3 &&
          InspectorMarkRowAtPoint(NSMakePoint(30,NSMaxY(lastRow)+1),compact)==NSNotFound,
          "body hit testing selects marks only, never a reference row");
    const NSRect wide=InspectorAspectFit(NSMakeSize(1600,900),compact.previewFrame);
    const NSRect tall=InspectorAspectFit(NSMakeSize(900,1600),compact.previewFrame);
    Check(fabs(NSWidth(wide)/NSHeight(wide)-1600.0/900.0)<0.0001 &&
          fabs(NSWidth(tall)/NSHeight(tall)-900.0/1600.0)<0.0001 &&
          NSContainsRect(compact.previewFrame,wide) &&
          NSContainsRect(compact.previewFrame,tall),
          "wide and tall previews fit their visible frame without distortion");
    Check(NSIsEmptyRect(InspectorAspectFit(NSMakeSize(0,900),compact.previewFrame)) &&
          NSIsEmptyRect(InspectorAspectFit(NSMakeSize(900,0),compact.previewFrame)),
          "zero-dimension image cannot draw a stale or invalid fitted preview");
    mark.note="First line\n第二行 😀\n"+std::string(350,'x');
    NSString* exactNote=[[NSString alloc] initWithBytes:mark.note.data()
        length:mark.note.size() encoding:NSUTF8StringEncoding];
    NSString* expectedRow=[@"7  " stringByAppendingString:exactNote];
    NSString* expected=[@"Selected reference\nMark 7\nNote:\n"
        stringByAppendingString:exactNote];
    Check([InspectorMarkRowText(mark) isEqualToString:expectedRow] &&
          [InspectorSelectedMarkText(mark) isEqualToString:expected],
          "long Unicode multiline note displays without alteration in row and detail");
    MarkSnapshot otherMark;
    otherMark.number=8;
    otherMark.note="Different note";
    Check([InspectorMarkRowText(otherMark) isEqualToString:@"8  Different note"] &&
          ![InspectorMarkRowText(otherMark) containsString:exactNote] &&
          ![InspectorMarkRowText(mark) containsString:@"Different note"],
          "distinct persisted marks keep their own number and note text");
    NSArray<NSString*>* longRows=@[InspectorMarkRowText(mark),shortRows[1],
        shortRows[2],shortRows[3]];
    const auto longNote=InspectorBodyLayout(470,270,480,longRows,YES);
    Check(NSHeight(longNote.previewFrame)==180 &&
          NSEqualRects(longNote.previewFrame,compact.previewFrame) &&
          NSHeight(longNote.markRowFrames.front())>NSHeight(firstRow) &&
          NSMinY(longNote.markRowFrames[1])>NSMaxY(longNote.markRowFrames.front()) &&
          longNote.documentHeight>compact.documentHeight,
          "long note wraps in its own row and grows the scroll document");

    const char* a="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    const char* b="bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    const char* c="cccccccccccccccccccccccccccccccc";
    STDeleteAllReferencesAction action;
    Check(!action.Begin({}),"empty store has no action");
    const std::vector<ReferenceJobSnapshot> initial={
      Job(a,ReferenceJobState::kReady),Job(b,ReferenceJobState::kPending),
      Job(c,ReferenceJobState::kDeleted)};
    const auto cancelled=action.Begin(initial);
    Check(cancelled && cancelled->ids==std::vector<std::string>({a,b}),
          "dialog snapshot preserves exact visible order and omits tombstones");
    Check(!action.Begin({Job(c,ReferenceJobState::kReady)}),
          "repeated activation cannot replace an open review");
    Check(action.Cancel(cancelled->generation),"Cancel closes the matching review");
    Check(!action.hasPendingReview(),"Cancel does not retain an unconfirmed set");
    int deletes=0;
    Check(!action.Confirm(cancelled->generation,[&](std::string_view) {
      ++deletes;return DeleteResult::kDeleted;
    }) && deletes==0,"cancelled callback cannot delete");

    const auto reviewed=action.Begin(initial);
    Check(reviewed && reviewed->ids==std::vector<std::string>({a,b}),
          "new review freezes the current ordered IDs");
    std::vector<std::string> called;
    const auto partial=action.Confirm(reviewed->generation,
        [&](std::string_view id) {
          called.emplace_back(id);
          return id==a?DeleteResult::kDeleted:DeleteResult::kCleanupFailed;
        });
    Check(partial && partial->deleted==1 && partial->cleanup_failed==1 &&
          called==reviewed->ids,"partial cleanup reports each reviewed result");
    Check(action.hasPendingReview(),"failed cleanup preserves reviewed IDs");
    Check(!action.Confirm(reviewed->generation,[&](std::string_view) {
      ++deletes;return DeleteResult::kDeleted;
    }) && deletes==0,"stale completion cannot repeat deletion");

    // A reference created during the dialog or before retry is outside the set.
    const auto retry=action.Begin({Job(a,ReferenceJobState::kDeleted),
        Job(b,ReferenceJobState::kDeleted),Job(c,ReferenceJobState::kReady)});
    Check(retry && retry->ids==reviewed->ids,
          "retry keeps original IDs despite later references and tombstones");
    called.clear();
    const auto complete=action.Confirm(retry->generation,[&](std::string_view id) {
      called.emplace_back(id);
      return id==a?DeleteResult::kAlreadyDeleted:DeleteResult::kDeleted;
    });
    Check(complete && complete->already_deleted==1 && complete->deleted==1 &&
          called==reviewed->ids && !action.hasPendingReview(),
          "retry cleans the same reviewed set and then releases it");
    const auto next=action.Begin({Job(c,ReferenceJobState::kReady)});
    Check(next && next->ids==std::vector<std::string>({c}),
          "later references survive and require their own review");
    Check(action.Cancel(next->generation),"new review can be cancelled");
  }
  std::cout<<"Inspector reference layout and Delete All tests passed\n";
}
