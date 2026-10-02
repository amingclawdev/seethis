#define SEETHIS_INSPECTOR_FEEDBACK_TESTING 1
#import "../src/platform/mac/overlay_mac.mm"
#include <cstdlib>
#include <iostream>

@interface STFakeFeedbackAdapter : NSObject <STInspectorFeedbackAdapter>
@property(nonatomic) BOOL handlerAvailable;
@property(nonatomic) BOOL openSucceeds;
@property(nonatomic) BOOL copySucceeds;
@property(nonatomic) NSUInteger handlerChecks;
@property(nonatomic) NSUInteger opens;
@property(nonatomic) NSUInteger copies;
@property(nonatomic,strong) NSURL* requestedURL;
@property(nonatomic,copy) NSString* copiedAddress;
@end
@implementation STFakeFeedbackAdapter
- (BOOL)hasMailHandlerForURL:(NSURL*)url {
  ++_handlerChecks;_requestedURL=url;return _handlerAvailable;
}
- (BOOL)openMailURL:(NSURL*)url {
  ++_opens;_requestedURL=url;return _openSucceeds;
}
- (BOOL)copyEmailAddress:(NSString*)address {
  ++_copies;_copiedAddress=address;return _copySucceeds;
}
@end

@interface STFakeChromeAdapter : NSObject <STInspectorChromeAdapter>
@property(nonatomic) seethis::platform::ChromeConnectionSnapshot connectionState;
@property(nonatomic) seethis::platform::EffectiveShortcutSnapshot bindingState;
@property(nonatomic) NSUInteger connects;
@property(nonatomic) NSUInteger retries;
@property(nonatomic) NSUInteger settingsOpens;
@property(nonatomic) BOOL settingsSucceeds;
@property(nonatomic) std::int64_t activeChromePID;
@end
@implementation STFakeChromeAdapter
- (seethis::platform::ChromeConnectionSnapshot)connection {return _connectionState;}
- (seethis::platform::EffectiveShortcutSnapshot)shortcuts {return _bindingState;}
- (void)connect {++_connects;}
- (void)retry {++_retries;}
- (BOOL)openSettings {++_settingsOpens;return _settingsSucceeds;}
- (std::int64_t)foregroundChromePID {return _activeChromePID;}
@end

static void Check(bool condition,const char* message) {
  if(!condition){std::cerr<<message<<'\n';std::exit(1);}
}
static void CheckURL(NSURL* url) {
  Check([url.absoluteString isEqualToString:
      @"mailto:z5866318@gmail.com?subject=SeeThis%20Feedback"],"exact mailto encoding");
  NSURLComponents* components=[NSURLComponents componentsWithURL:url resolvingAgainstBaseURL:NO];
  Check([components.scheme isEqualToString:@"mailto"] &&
        [components.path isEqualToString:@"z5866318@gmail.com"],"exact recipient");
  Check(components.queryItems.count==1 &&
        [components.queryItems.firstObject.name isEqualToString:@"subject"] &&
        [components.queryItems.firstObject.value isEqualToString:@"SeeThis Feedback"] &&
        !components.fragment && !components.host,"subject only; no body, attachment, or private payload");
}
static void CheckInside(NSRect frame,NSRect bounds,const char* message) {
  Check(NSMinX(frame)>=0 && NSMinY(frame)>=0 &&
        NSMaxX(frame)<=NSWidth(bounds) && NSMaxY(frame)<=NSHeight(bounds),message);
}

static void TestProductionChromeView() {
  using namespace seethis::platform;
  STFakeChromeAdapter* fake=[STFakeChromeAdapter new];
  STInspectorChromeView* chrome=[[STInspectorChromeView alloc] initWithAdapter:fake];
  STFakeFeedbackAdapter* mail=[STFakeFeedbackAdapter new];
  STInspectorFeedbackView* feedback=[[STInspectorFeedbackView alloc] initWithAdapter:mail];
  STInspectorContainerView* container=[[STInspectorContainerView alloc]
      initWithFrame:NSMakeRect(0,0,470,430)];
  NSScrollView* scroll=[NSScrollView new];
  NSView* document=[[NSView alloc] initWithFrame:NSMakeRect(0,0,470,2000)];
  scroll.documentView=document;
  container.feedbackView=feedback;container.chromeView=chrome;
  container.referenceScrollView=scroll;
  [container addSubview:feedback];[container addSubview:chrome];[container addSubview:scroll];
  [container layout];
  Check(fake.connects==0 && fake.retries==0 && fake.settingsOpens==0,
        "production view initialization has no permission/system action");
  Check(chrome.stateLabel.hidden && !chrome.shortcutLabel.hidden,"startup unknown hides recovery and keeps shortcuts");
  const CGFloat compactHeight=NSHeight(chrome.frame);
  ChromeConnectionSnapshot healthy;
  healthy.target={51,123};healthy.permission={ChromeConsent::kGranted,0};
  healthy.provider=ChromeProviderState::kReady;
  EffectiveShortcutSnapshot bindings;
  bindings.capture={11,seethis::core::kShortcutCommand,true};
  bindings.deletion={7,seethis::core::kShortcutControl|seethis::core::kShortcutShift,true};
  bindings.capture_state=CaptureShortcutState::kReady;
  bindings.delete_state=DeleteShortcutState::kReady;bindings.screen=ScreenRecordingState::kReady;
  fake.bindingState=bindings;
  for(const auto provider:{ChromeProviderState::kReady,ChromeProviderState::kNoPage}) {
    healthy.provider=provider;
    for(const auto page:{seethis::core::PageAvailability::kUnavailable,
                        seethis::core::PageAvailability::kReady}) {
      healthy.page=page;healthy.observation_active=false;fake.connectionState=healthy;
      [chrome refreshChrome];[container layout];
      Check(chrome.stateLabel.hidden && chrome.guidanceLabel.hidden &&
            !chrome.shortcutLabel.hidden,"healthy view independent of page/foreground observation");
      for(NSButton* button in chrome.buttons)Check(button.hidden,"all three recovery actions collapse");
      Check([chrome.shortcutLabel.stringValue containsString:@"⌘B"] &&
            [chrome.shortcutLabel.stringValue containsString:@"⌃⇧X"],
            "actual nondefault registered bindings rendered");
      Check(NSHeight(chrome.frame)==compactHeight &&
            NSMinY(scroll.frame)==NSMaxY(chrome.frame),"no abandoned recovery area in healthy layout");
    }
  }
  bindings.capture={12,seethis::core::kShortcutOption,true};
  fake.bindingState=bindings;[chrome refreshChrome];[container layout];
  Check([chrome.shortcutLabel.stringValue containsString:@"⌥Q"] &&
        ![chrome.shortcutLabel.stringValue containsString:@"⌘B"],"live effective registration change replaces stale shortcut");
  bindings.capture.registered=false;bindings.capture_state=CaptureShortcutState::kRegistrationFailed;
  bindings.deletion.registered=false;bindings.delete_state=DeleteShortcutState::kConflict;
  fake.bindingState=bindings;[chrome refreshChrome];
  Check([chrome.shortcutLabel.stringValue containsString:@"Unavailable — registration failed"] &&
        [chrome.shortcutLabel.stringValue containsString:@"Unavailable — conflict"] &&
        ![chrome.shortcutLabel.stringValue containsString:@"⌥Q"],"failed/conflicting attempt never advertised as active");
  bindings.capture.registered=true;bindings.capture_state=CaptureShortcutState::kInputUnavailable;
  bindings.screen=ScreenRecordingState::kDenied;fake.bindingState=bindings;[chrome refreshChrome];
  Check([chrome.shortcutLabel.stringValue containsString:@"input unavailable"] &&
        [chrome.shortcutLabel.stringValue containsString:@"Screen Recording unavailable"],
        "registered binding blocked by capability remains truthful");
  bindings={};fake.bindingState=bindings;[chrome refreshChrome];
  Check([chrome.shortcutLabel.stringValue containsString:@"Unavailable — unknown"],"stop clears effective binding display");
  for(const auto provider:{ChromeProviderState::kUnknown,ChromeProviderState::kChecking,
                          ChromeProviderState::kDenied,ChromeProviderState::kAmbiguous,
                          ChromeProviderState::kTimedOut,ChromeProviderState::kFailed}) {
    healthy.provider=provider;fake.connectionState=healthy;
    [chrome refreshChrome];[container layout];
    Check(chrome.stateLabel.hidden && chrome.guidanceLabel.hidden && !chrome.shortcutLabel.hidden,
          "granted provider failure hides entire permission block and preserves shortcuts");
    for(NSButton* button in chrome.buttons)Check(button.hidden,"provider error alone never displays permission recovery");
  }
  healthy.provider=ChromeProviderState::kReady;healthy.permission.consent=ChromeConsent::kDenied;
  fake.connectionState=healthy;fake.activeChromePID=51;[chrome refreshChrome];
  Check(chrome.buttons[0].hidden,"frontmost denied Chrome alone cannot arm initial startup");
  chrome.chromeContextActive=YES;[chrome refreshChrome];[container layout];
  Check(!chrome.buttons[0].hidden && !chrome.shortcutLabel.hidden,"actual poststartup Chrome context with denial permits recovery and shortcuts");
  const CGFloat recoveryHeight=NSHeight(chrome.frame);
  fake.activeChromePID=0;[chrome refreshChrome];[container layout];
  Check(chrome.buttons[0].hidden && !chrome.chromeContextActive && NSHeight(chrome.frame)<recoveryHeight,
        "leaving Chrome/self context synchronously clears activation and reclaims recovery height");
  fake.activeChromePID=51;[chrome refreshChrome];
  Check(chrome.buttons[0].hidden,"returning snapshot alone cannot re-arm without real Chrome event");
  chrome.chromeContextActive=YES;[chrome refreshChrome];
  healthy.permission.consent=ChromeConsent::kGranted;healthy.target={};
  fake.connectionState=healthy;[chrome refreshChrome];Check(chrome.stateLabel.hidden,"process loss is not a current permission-denied context");
  healthy.target={51,123};healthy.permission.consent=ChromeConsent::kDenied;
  fake.connectionState=healthy;fake.activeChromePID=51;chrome.chromeContextActive=YES;[chrome refreshChrome];
  for(NSNumber* width in @[@470,@454,@320,@250]) {
    container.frame=NSMakeRect(0,0,width.doubleValue,430);[container layout];
    CheckInside(chrome.stateLabel.frame,chrome.bounds,"wrapped actual state fits");
    CheckInside(chrome.guidanceLabel.frame,chrome.bounds,"wrapped recovery help fits");
    for(NSUInteger i=0;i<chrome.buttons.count;++i) {
      CheckInside(chrome.buttons[i].frame,chrome.bounds,"recovery button fits narrow host");
      for(NSUInteger j=i+1;j<chrome.buttons.count;++j)
        Check(!NSIntersectsRect(chrome.buttons[i].frame,chrome.buttons[j].frame),"recovery buttons never overlap");
    }
    Check(NSHeight(scroll.frame)>0 && scroll.documentView==document &&
          !feedback.feedbackButton.hidden,"470 and narrower preserve Feedback/reference space");
  }
  std::cerr << "Chrome action fixture before clicks: NSApp=" << (NSApp ? "present" : "nil") << '\n';
  [chrome.buttons[0] performClick:nil];[chrome.buttons[1] performClick:nil];
  [chrome.buttons[2] performClick:nil];[container layout];
  std::cerr << "Chrome action fixture after clicks: NSApp=" << (NSApp ? "present" : "nil")
            << " connects=" << fake.connects << " retries=" << fake.retries
            << " settingsOpens=" << fake.settingsOpens
            << " guidance=" << chrome.guidanceLabel.stringValue.UTF8String << '\n';
  Check(fake.connects==1 && fake.retries==1 && fake.settingsOpens==1 &&
        [chrome.guidanceLabel.stringValue containsString:@"Could not open Settings"],
        "production actions wire explicit fake adapters and truthful failure");
  healthy.target={51,123};healthy.provider=ChromeProviderState::kNoPage;healthy.permission.consent=ChromeConsent::kGranted;
  fake.connectionState=healthy;fake.bindingState=bindings;
  [chrome refreshChrome];[container layout];
  Check(chrome.stateLabel.hidden && chrome.buttons[0].hidden,"recovery-to-healthy transition collapses immediately");
  Check(mail.opens==0 && mail.copies==0,"Chrome view test never sends mail or copies");
}

int main() {
  @autoreleasepool {
    // Production initializes NSApplication before AppKit dispatches button actions.
    (void)[NSApplication sharedApplication];
    TestProductionChromeView();
    CheckURL(STFeedbackMailURL());
    STFakeFeedbackAdapter* fake=[STFakeFeedbackAdapter new];
    STInspectorFeedbackController* action=[[STInspectorFeedbackController alloc] initWithAdapter:fake];
    Check(fake.opens==0 && fake.copies==0 && !action.status && !action.copyAddressAvailable,
          "initialization has no external action");
    [action copyAddress];
    Check(fake.copies==0,"hidden fallback cannot copy");
    [action compose];
    Check(fake.handlerChecks==1 && fake.opens==0 && fake.copies==0 && action.copyAddressAvailable,
          "no handler is an explicit failure without opening or copying");
    Check([action.status containsString:@"Could not open"],"truthful absent-handler feedback");
    fake.handlerAvailable=YES;
    [action compose];
    Check(fake.opens==1 && fake.copies==0 && action.copyAddressAvailable,
          "open false preserves explicit copy fallback and clipboard");
    CheckURL(fake.requestedURL);
    Check([action.status containsString:@"Could not open"],"truthful open-false feedback");
    fake.copySucceeds=NO;
    [action copyAddress];
    Check(fake.copies==1 && [fake.copiedAddress isEqualToString:@"z5866318@gmail.com"] &&
          [action.status containsString:@"Could not copy"],"truthful explicit copy failure");
    fake.copySucceeds=YES;
    [action copyAddress];
    Check(fake.copies==2 && [action.status isEqualToString:@"Email address copied."],
          "explicit copy writes only public email address");
    fake.openSucceeds=YES;
    [action compose];
    Check(fake.opens==2 && fake.copies==2 && !action.copyAddressAvailable &&
          [action.status containsString:@"compose requested"],"open success requests compose, never claims sent");
    [action copyAddress];
    Check(fake.copies==2,"successful compose does not mutate clipboard");

    // Real AppKit views, fake external adapter: no window, app run, mail or pasteboard.
    STFakeFeedbackAdapter* viewFake=[STFakeFeedbackAdapter new];
    STInspectorFeedbackView* header=[[STInspectorFeedbackView alloc] initWithAdapter:viewFake];
    STInspectorContainerView* container=[[STInspectorContainerView alloc]
        initWithFrame:NSMakeRect(0,0,470,430)];
    NSScrollView* scroll=[[NSScrollView alloc] initWithFrame:NSZeroRect];
    NSView* document=[[NSView alloc] initWithFrame:NSMakeRect(0,0,470,2000)];
    scroll.documentView=document;
    container.feedbackView=header;container.referenceScrollView=scroll;
    [container addSubview:header];[container addSubview:scroll];
    Check(!header.feedbackButton.hidden && header.feedbackButton.enabled &&
          header.addressFallbackButton.hidden,"Feedback available with no selected reference");
    for(NSNumber* width in @[@470,@454,@320,@250]) {
      container.frame=NSMakeRect(0,0,width.doubleValue,430);
      [container layout];
      CheckInside(header.feedbackButton.frame,header.bounds,"initial Feedback fits header");
      Check(NSMinY(scroll.frame)>=NSMaxY(header.frame),"reference body below fixed header");
      [header.feedbackButton performClick:nil];
      [container layout];
      Check(!header.addressFallbackButton.hidden && viewFake.copies==0,"failure enables explicit fallback only");
      CheckInside(header.feedbackButton.frame,header.bounds,"Feedback fits failure header");
      CheckInside(header.addressFallbackButton.frame,header.bounds,"copy fallback fits narrow header");
      CheckInside(header.statusLabel.frame,header.bounds,"wrapped feedback fits header");
      Check(!NSIntersectsRect(header.feedbackButton.frame,header.addressFallbackButton.frame),
            "action buttons do not overlap");
      Check(NSMinY(header.statusLabel.frame)>=NSMaxY(header.addressFallbackButton.frame) &&
            NSMinY(scroll.frame)>=NSMaxY(header.frame),"status and reference body do not overlap");
      Check(scroll.documentView==document,"existing reference document retained");
      [scroll.contentView scrollToPoint:NSMakePoint(0,500)];
      Check(NSMinY(header.frame)==0,"Feedback stays at top while references scroll");
    }
    [header.addressFallbackButton performClick:nil];[container layout];
    Check(viewFake.copies==1 && [viewFake.copiedAddress isEqualToString:@"z5866318@gmail.com"],
          "fallback button wires explicit public-address copy");
    viewFake.handlerAvailable=YES;viewFake.openSucceeds=YES;
    [header.feedbackButton performClick:nil];[container layout];
    Check(viewFake.opens==1 && viewFake.copies==1 && header.addressFallbackButton.hidden,
          "Feedback button requests compose and hides stale fallback on success");
    CheckURL(viewFake.requestedURL);
  }
  std::cout<<"Inspector feedback action and layout tests passed\n";
}
