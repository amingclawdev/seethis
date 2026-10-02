#define SEETHIS_INSPECTOR_FEEDBACK_TESTING 1
#import "../src/platform/mac/overlay_mac.mm"
#include <cstdlib>
#include <iostream>

// Model window-server identities without ordering any desktop windows.
@interface STCaptureTestPanel : NSPanel
@property(nonatomic) NSInteger testWindowNumber;
@property(nonatomic) BOOL testVisible;
@end
@implementation STCaptureTestPanel
- (NSInteger)windowNumber {return _testWindowNumber;}
- (BOOL)isVisible {return _testVisible;}
- (void)close {[super close];_testWindowNumber=0;_testVisible=NO;}
@end

static STCaptureTestPanel* Panel(NSInteger number) {
  STCaptureTestPanel* panel=[[STCaptureTestPanel alloc]
      initWithContentRect:NSMakeRect(0,0,20,20)
      styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:YES];
  panel.releasedWhenClosed=NO;
  panel.testWindowNumber=number;
  return panel;
}

static void Expect(NSArray<NSWindow*>* panels,NSWindow* inspector,
                   std::initializer_list<std::uint64_t> expected,const char* message) {
  if(STCaptureExcludedWindowIDs(panels,inspector)!=
      std::vector<std::uint64_t>(expected)) {
    std::cerr<<message<<'\n';std::exit(1);
  }
}

int main() {
  @autoreleasepool {
    (void)[NSApplication sharedApplication];
    STCaptureTestPanel* first=Panel(101);
    STCaptureTestPanel* second=Panel(202);
    NSArray<NSWindow*>* panels=@[first,second];
    Expect(nil,nil,{},"uncreated windows have no exclusions");
    Expect(panels,nil,{101,202},"preserve both per-display drawing panels");

    STCaptureTestPanel* inspector=Panel(303);
    Expect(panels,inspector,{101,202,303},
        "hidden Inspector must be excluded before asynchronous capture");
    inspector.testVisible=YES;
    Expect(panels,inspector,{101,202,303},"shown Inspector is excluded");
    inspector.testVisible=NO;
    Expect(panels,inspector,{101,202,303},"hiding does not drop its live identity");
    inspector.testWindowNumber=404;
    Expect(panels,inspector,{101,202,404},"read current Inspector number, never stale ID");
    [inspector close];
    Expect(panels,inspector,{101,202},"closed invalid Inspector is safely omitted");
    inspector=Panel(505);
    Expect(panels,inspector,{101,202,505},"reopened replacement uses its new identity");

    STCaptureTestPanel* duplicate=Panel(101);
    STCaptureTestPanel* zero=Panel(0);
    STCaptureTestPanel* invalid=Panel(-1);
    NSArray<NSWindow*>* mixed=@[first,second,duplicate,zero,invalid];
    inspector.testWindowNumber=101;
    Expect(mixed,inspector,{101,202},"deduplicate panels and Inspector; omit invalid IDs");
    inspector.testWindowNumber=-7;
    Expect(mixed,inspector,{101,202},"negative Inspector must not become unsigned ID");
    inspector.testWindowNumber=0;
    Expect(mixed,inspector,{101,202},"unassigned Inspector is omitted");

    STCaptureTestPanel* target=Panel(999);
    target.testVisible=YES;
    inspector.testWindowNumber=606;
    Expect(panels,inspector,{101,202,606},
        "exclude only owned drawing panels and Inspector, never unrelated target window");
    std::cout<<"capture exclusion: 12 production-assembler cases passed; no windows ordered\n";
  }
  return 0;
}
