#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <os/log.h>
#include "platform/platform.h"

static NSString* STFeedbackEmailAddress() {return @"z5866318@gmail.com";}
static NSURL* STFeedbackMailURL() {
  NSURLComponents* components=[NSURLComponents componentsWithString:
      [@"mailto:" stringByAppendingString:STFeedbackEmailAddress()]];
  components.queryItems=@[[NSURLQueryItem queryItemWithName:@"subject"
                                                   value:@"SeeThis Feedback"]];
  return components.URL;
}

@protocol STInspectorFeedbackAdapter <NSObject>
- (BOOL)hasMailHandlerForURL:(NSURL*)url;
- (BOOL)openMailURL:(NSURL*)url;
- (BOOL)copyEmailAddress:(NSString*)address;
@end
@interface STSystemInspectorFeedbackAdapter : NSObject <STInspectorFeedbackAdapter>
@end
@implementation STSystemInspectorFeedbackAdapter
- (BOOL)hasMailHandlerForURL:(NSURL*)url {
  return [NSWorkspace.sharedWorkspace URLForApplicationToOpenURL:url]!=nil;
}
- (BOOL)openMailURL:(NSURL*)url {return [NSWorkspace.sharedWorkspace openURL:url];}
- (BOOL)copyEmailAddress:(NSString*)address {
  NSPasteboard* pasteboard=NSPasteboard.generalPasteboard;
  [pasteboard clearContents];
  return [pasteboard setString:address forType:NSPasteboardTypeString];
}
@end

@interface STInspectorFeedbackController : NSObject
@property(nonatomic,strong) id<STInspectorFeedbackAdapter> adapter;
@property(nonatomic,copy) NSString* status;
@property(nonatomic) BOOL copyAddressAvailable;
- (instancetype)initWithAdapter:(id<STInspectorFeedbackAdapter>)adapter;
- (void)compose;
- (void)copyAddress;
@end
@implementation STInspectorFeedbackController
- (instancetype)initWithAdapter:(id<STInspectorFeedbackAdapter>)adapter {
  self=[super init];if(self)_adapter=adapter;return self;
}
- (void)compose {
  NSURL* url=STFeedbackMailURL();
  const BOOL requested=url && [self.adapter hasMailHandlerForURL:url] &&
      [self.adapter openMailURL:url];
  self.copyAddressAvailable=!requested;
  self.status=requested
      ? @"Email compose requested. Send your feedback from your email app."
      : @"Could not open your email app. Copy the email address to send feedback.";
}
- (void)copyAddress {
  if(!self.copyAddressAvailable)return;
  self.status=[self.adapter copyEmailAddress:STFeedbackEmailAddress()]
      ? @"Email address copied." : @"Could not copy the email address.";
}
@end

@interface STInspectorFeedbackView : NSView
@property(nonatomic,strong) STInspectorFeedbackController* feedback;
@property(nonatomic,strong) NSButton* feedbackButton;
@property(nonatomic,strong) NSButton* addressFallbackButton;
@property(nonatomic,strong) NSTextField* statusLabel;
- (instancetype)initWithAdapter:(id<STInspectorFeedbackAdapter>)adapter;
- (CGFloat)layoutForWidth:(CGFloat)width;
@end
@implementation STInspectorFeedbackView
- (BOOL)isFlipped {return YES;}
- (instancetype)initWithAdapter:(id<STInspectorFeedbackAdapter>)adapter {
  self=[super initWithFrame:NSZeroRect];
  if(self) {
    _feedback=[[STInspectorFeedbackController alloc] initWithAdapter:adapter];
    _feedbackButton=[NSButton buttonWithTitle:@"Feedback…" target:self
                                     action:@selector(composeFeedback:)];
    _feedbackButton.toolTip=@"Compose an email in your default email app.";
    _feedbackButton.accessibilityLabel=@"Compose feedback email";
    _addressFallbackButton=[NSButton buttonWithTitle:@"Copy email address" target:self
                                        action:@selector(copyFeedbackAddress:)];
    _addressFallbackButton.toolTip=STFeedbackEmailAddress();
    _addressFallbackButton.hidden=YES;
    for(NSButton* button in @[_feedbackButton,_addressFallbackButton]) {
      button.font=[NSFont systemFontOfSize:14 weight:NSFontWeightMedium];
      [self addSubview:button];
    }
    _statusLabel=[NSTextField wrappingLabelWithString:@""];
    _statusLabel.font=[NSFont systemFontOfSize:12];
    _statusLabel.hidden=YES;
    [self addSubview:_statusLabel];
  }
  return self;
}
- (void)refreshFeedback {
  self.addressFallbackButton.hidden=!self.feedback.copyAddressAvailable;
  self.statusLabel.stringValue=self.feedback.status?:@"";
  self.statusLabel.hidden=self.statusLabel.stringValue.length==0;
  [self.superview setNeedsLayout:YES];
}
- (void)composeFeedback:(id)sender {
  (void)sender;[self.feedback compose];[self refreshFeedback];
}
- (void)copyFeedbackAddress:(id)sender {
  (void)sender;[self.feedback copyAddress];[self refreshFeedback];
}
- (CGFloat)layoutForWidth:(CGFloat)width {
  const CGFloat available=MAX(1,width-32);
  const CGFloat feedbackWidth=MIN(available,ceil(self.feedbackButton.fittingSize.width)+12);
  const CGFloat copyWidth=MIN(available,ceil(self.addressFallbackButton.fittingSize.width)+12);
  self.feedbackButton.frame=NSMakeRect(16,4,feedbackWidth,34);
  CGFloat bottom=42;
  if(!self.addressFallbackButton.hidden) {
    const BOOL sameRow=feedbackWidth+8+copyWidth<=available;
    self.addressFallbackButton.frame=NSMakeRect(sameRow?width-16-copyWidth:16,
                                           sameRow?4:42,copyWidth,34);
    if(!sameRow)bottom=80;
  }
  if(!self.statusLabel.hidden) {
    const NSRect text=[self.statusLabel.stringValue boundingRectWithSize:
        NSMakeSize(available,CGFLOAT_MAX)
        options:NSStringDrawingUsesLineFragmentOrigin|NSStringDrawingUsesFontLeading
        attributes:@{NSFontAttributeName:self.statusLabel.font}];
    const CGFloat height=ceil(NSHeight(text))+2;
    self.statusLabel.frame=NSMakeRect(16,bottom,available,height);
    bottom+=height+8;
  }
  return bottom;
}
@end

@protocol STInspectorHeaderLayout <NSObject>
- (CGFloat)layoutForWidth:(CGFloat)width;
@optional
- (BOOL)compactHeader;
@end

@protocol STInspectorReferenceTarget <NSObject>
- (void)inspectorSelectReference:(NSString*)identifier;
@end

// The native menu retains the ID it displayed even if arrivals reorder history.
@interface STInspectorReferenceSelector : NSView <NSMenuDelegate>
@property(nonatomic,strong) NSTextField* label;
@property(nonatomic,strong) NSPopUpButton* popup;
@property(nonatomic,strong) NSTextField* timestampLabel;
@property(nonatomic,copy) NSArray<NSString*>* referenceTimes;
@property(nonatomic,weak) id<STInspectorReferenceTarget> target;
@property(nonatomic,copy) NSArray<NSString*>* referenceIDs;
@property(nonatomic,copy) NSArray<NSString*>* referenceRows;
@property(nonatomic,copy) NSString* selectedReferenceID;
@property(nonatomic) BOOL menuTracking;
- (void)refreshMenu;
- (void)chooseReference:(NSMenuItem*)item;
- (CGFloat)layoutForWidth:(CGFloat)width;
@end
@implementation STInspectorReferenceSelector
- (BOOL)isFlipped {return YES;}
- (instancetype)initWithFrame:(NSRect)frame {
  if((self=[super initWithFrame:frame])) {
    _label=[NSTextField labelWithString:@"References"];
    _label.font=[NSFont systemFontOfSize:12];[self addSubview:_label];
    _popup=[[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    _popup.font=[NSFont systemFontOfSize:12];
    _popup.menu.font=_popup.font;
    _popup.cell.lineBreakMode=NSLineBreakByTruncatingTail;
    _timestampLabel=[NSTextField labelWithString:@""];
    _timestampLabel.font=[NSFont systemFontOfSize:11];
    _timestampLabel.accessibilityLabel=@"Reference capture date and time";
    [self addSubview:_timestampLabel];
    _popup.accessibilityLabel=@"Select reference";
    _popup.menu.delegate=self;[self addSubview:_popup];
    [self refreshMenu];
  }
  return self;
}
- (void)refreshMenu {
  if(self.menuTracking)return;
  [self.popup removeAllItems];
  [self.popup addItemWithTitle:self.referenceIDs.count?@"Select a reference…":@"No references"];
  self.popup.itemArray.firstObject.enabled=NO;
  self.popup.enabled=self.referenceIDs.count>0;
  self.timestampLabel.stringValue=@"";
  for(NSUInteger i=0;i<self.referenceIDs.count;++i) {
    NSString* identifier=self.referenceIDs[i];
    [self.popup addItemWithTitle:self.referenceRows[i]];
    NSMenuItem* item=self.popup.lastItem;
    item.representedObject=identifier;item.target=self;
    item.action=@selector(chooseReference:);item.toolTip=identifier;
    if([identifier isEqualToString:self.selectedReferenceID]) {
      [self.popup selectItem:item];
      if(i<self.referenceTimes.count)self.timestampLabel.stringValue=self.referenceTimes[i];
    }
  }
}
- (void)chooseReference:(NSMenuItem*)item {
  NSString* identifier=item.representedObject;
  if([identifier isKindOfClass:NSString.class] && identifier.length)
    [self.target inspectorSelectReference:identifier];
}
- (void)menuWillOpen:(NSMenu*)menu {(void)menu;self.menuTracking=YES;}
- (void)menuDidClose:(NSMenu*)menu {
  (void)menu;self.menuTracking=NO;[self refreshMenu];
}
- (CGFloat)layoutForWidth:(CGFloat)width {
  const CGFloat available=MAX(1,width-32);
  self.label.frame=NSMakeRect(16,0,available,16);
  self.popup.frame=NSMakeRect(16,16,available,26);
  self.timestampLabel.frame=NSMakeRect(16,42,available,16);
  return 64;
}
@end

// Keep the app action visible even when the reference document is scrolled.
@protocol STInspectorReferenceLayout <NSObject>
- (void)fitToWidth:(CGFloat)width;
@end

@interface STInspectorContainerView : NSView
@property(nonatomic,strong) STInspectorFeedbackView* feedbackView;
@property(nonatomic,strong) NSView<STInspectorHeaderLayout>* chromeView;
@property(nonatomic,strong) STInspectorReferenceSelector* referenceSelector;
@property(nonatomic,strong) NSScrollView* referenceScrollView;
@end
@implementation STInspectorContainerView
- (BOOL)isFlipped {return YES;}
- (void)resizeSubviewsWithOldSize:(NSSize)size {
  [super resizeSubviewsWithOldSize:size];[self setNeedsLayout:YES];
}
- (void)layout {
  [super layout];
  const CGFloat width=NSWidth(self.bounds);
  const CGFloat feedbackHeight=[self.feedbackView layoutForWidth:width];
  self.feedbackView.frame=NSMakeRect(0,0,width,feedbackHeight);
  const BOOL beside=self.referenceSelector && width>=430 &&
      [self.chromeView respondsToSelector:@selector(compactHeader)] &&
      [self.chromeView compactHeader];
  const CGFloat chromeWidth=beside?width-220:width;
  const CGFloat chromeHeight=self.chromeView?[self.chromeView layoutForWidth:chromeWidth]:0;
  self.chromeView.frame=NSMakeRect(0,feedbackHeight,chromeWidth,chromeHeight);
  const CGFloat selectorHeight=self.referenceSelector?
      [self.referenceSelector layoutForWidth:beside?220:width]:0;
  self.referenceSelector.frame=NSMakeRect(beside?chromeWidth:0,
      feedbackHeight+(beside?0:chromeHeight),beside?220:width,selectorHeight);
  const CGFloat header=feedbackHeight+(beside?MAX(chromeHeight,selectorHeight):
      chromeHeight+selectorHeight);
  self.referenceScrollView.frame=NSMakeRect(0,header,width,MAX(0,NSHeight(self.bounds)-header));
  [self.referenceScrollView tile];
  NSView* document=self.referenceScrollView.documentView;
  if([document respondsToSelector:@selector(fitToWidth:)])
    [(id<STInspectorReferenceLayout>)document fitToWidth:
        NSWidth(self.referenceScrollView.contentView.bounds)];
}
@end

// Tests compile this actual production view and inject only external adapters.
@protocol STInspectorChromeAdapter <NSObject>
- (seethis::platform::ChromeConnectionSnapshot)connection;
- (seethis::platform::EffectiveShortcutSnapshot)shortcuts;
- (void)connect;
- (void)retry;
- (BOOL)openSettings;
@optional
- (std::int64_t)foregroundChromePID;
@end
#if !defined(SEETHIS_INSPECTOR_FEEDBACK_TESTING)
@interface STSystemInspectorChromeAdapter : NSObject <STInspectorChromeAdapter>
@end
@implementation STSystemInspectorChromeAdapter
- (seethis::platform::ChromeConnectionSnapshot)connection {
  return seethis::platform::ChromeConnectionStatus();
}
- (seethis::platform::EffectiveShortcutSnapshot)shortcuts {
  return seethis::platform::InputShortcutStatus();
}
- (void)connect {seethis::platform::ConnectChromeExplicit();}
- (void)retry {seethis::platform::RetryChromeExplicit();}
- (BOOL)openSettings {return seethis::platform::OpenChromeAutomationSettingsExplicit();}
- (std::int64_t)foregroundChromePID {
  NSRunningApplication* app=NSWorkspace.sharedWorkspace.frontmostApplication;
  return [app.bundleIdentifier isEqualToString:@"com.google.Chrome"]?app.processIdentifier:0;
}
@end
#endif

static NSString* STShortcutText(seethis::platform::EffectiveShortcutBinding binding,
                               std::string_view status) {
  NSString* readiness=[NSString stringWithUTF8String:std::string(status).c_str()];
  readiness=[readiness stringByReplacingOccurrencesOfString:@"_" withString:@" "];
  if(!binding.registered)return [@"Unavailable — " stringByAppendingString:readiness];
  // Physical macOS ANSI key positions; unknown keys retain their exact code.
  NSString* key=nil;
  switch(binding.key_code) {
    case 0:key=@"A";break;case 1:key=@"S";break;case 2:key=@"D";break;
    case 3:key=@"F";break;case 4:key=@"H";break;case 5:key=@"G";break;
    case 6:key=@"Z";break;case 7:key=@"X";break;case 8:key=@"C";break;
    case 9:key=@"V";break;case 11:key=@"B";break;case 12:key=@"Q";break;
    case 13:key=@"W";break;case 14:key=@"E";break;case 15:key=@"R";break;
    case 16:key=@"Y";break;case 17:key=@"T";break;case 31:key=@"O";break;
    case 32:key=@"U";break;case 34:key=@"I";break;case 35:key=@"P";break;
    case 37:key=@"L";break;case 38:key=@"J";break;case 40:key=@"K";break;
    case 45:key=@"N";break;case 46:key=@"M";break;
    default:key=[NSString stringWithFormat:@"Key %u",binding.key_code];break;
  }
  NSMutableString* chord=[NSMutableString string];
  if(binding.modifiers & seethis::core::kShortcutControl)[chord appendString:@"⌃"];
  if(binding.modifiers & seethis::core::kShortcutOption)[chord appendString:@"⌥"];
  if(binding.modifiers & seethis::core::kShortcutShift)[chord appendString:@"⇧"];
  if(binding.modifiers & seethis::core::kShortcutCommand)[chord appendString:@"⌘"];
  [chord appendString:key];
  return [NSString stringWithFormat:@"%@ — %@",chord,readiness];
}

@interface STInspectorChromeView : NSView <STInspectorHeaderLayout>
@property(nonatomic,strong) id<STInspectorChromeAdapter> adapter;
@property(nonatomic,strong) NSTextField* stateLabel;
@property(nonatomic,strong) NSTextField* guidanceLabel;
@property(nonatomic,strong) NSTextField* shortcutLabel;
@property(nonatomic,strong) NSArray<NSButton*>* buttons;
@property(nonatomic) BOOL chromeContextActive;
- (instancetype)initWithAdapter:(id<STInspectorChromeAdapter>)adapter;
- (void)refreshChrome;
@end
@implementation STInspectorChromeView
- (BOOL)isFlipped {return YES;}
- (BOOL)compactHeader {return self.stateLabel.hidden;}
#if !defined(SEETHIS_INSPECTOR_FEEDBACK_TESTING)
- (instancetype)initWithFrame:(NSRect)frame {
  self=[self initWithAdapter:[STSystemInspectorChromeAdapter new]];
  self.frame=frame;return self;
}
#endif
- (instancetype)initWithAdapter:(id<STInspectorChromeAdapter>)adapter {
  if((self=[super initWithFrame:NSZeroRect])) {
    _adapter=adapter;
    _stateLabel=[NSTextField wrappingLabelWithString:@"Chrome: checking…"];
    _stateLabel.font=[NSFont systemFontOfSize:11];[self addSubview:_stateLabel];
    _guidanceLabel=[NSTextField wrappingLabelWithString:@""];
    _guidanceLabel.font=[NSFont systemFontOfSize:11];[self addSubview:_guidanceLabel];
    _shortcutLabel=[NSTextField wrappingLabelWithString:@""];
    _shortcutLabel.font=[NSFont systemFontOfSize:12];[self addSubview:_shortcutLabel];
    NSMutableArray<NSButton*>* buttons=[NSMutableArray array];
    for(NSArray* action in @[@[@"Connect Chrome",NSStringFromSelector(@selector(connectChrome:))],
                             @[@"Retry Chrome",NSStringFromSelector(@selector(retryChrome:))],
                             @[@"Automation Settings…",NSStringFromSelector(@selector(openAutomationSettings:))]]) {
      NSButton* button=[NSButton buttonWithTitle:action[0] target:self
          action:NSSelectorFromString(action[1])];
      button.font=[NSFont systemFontOfSize:11];[buttons addObject:button];[self addSubview:button];
    }
    _buttons=buttons;[self refreshChrome];
  }
  return self;
}
- (void)refreshChrome {
  const auto state=[self.adapter connection];
  const std::int64_t foreground=[self.adapter respondsToSelector:@selector(foregroundChromePID)]
      ?[self.adapter foregroundChromePID]:0;
  if(foreground==0)self.chromeContextActive=NO;
  const bool recovery=seethis::platform::ChromePermissionRecoveryVisible(
      state,self.chromeContextActive?foreground:0);
  auto text=[](std::string_view value) {
    return [NSString stringWithUTF8String:std::string(value).c_str()];
  };
  self.stateLabel.hidden=!recovery;self.guidanceLabel.hidden=!recovery;
  self.shortcutLabel.hidden=NO;
  for(NSButton* button in self.buttons)button.hidden=!recovery;
  {
    const auto bindings=[self.adapter shortcuts];
    NSString* capture=STShortcutText(bindings.capture,
        seethis::platform::CaptureShortcutStateName(bindings.capture_state));
    if(bindings.capture.registered &&
       !seethis::platform::ScreenRecordingReady(bindings.screen))
      capture=[capture stringByAppendingString:@" · Screen Recording unavailable"];
    self.shortcutLabel.stringValue=[NSString stringWithFormat:@"Capture: %@\nDelete: %@",
        capture,STShortcutText(bindings.deletion,
            seethis::platform::DeleteShortcutStateName(bindings.delete_state))];
  }
  if(recovery) {
    self.stateLabel.stringValue=[NSString stringWithFormat:@"Chrome Automation: %@",
        text(seethis::platform::ChromeConsentName(state.permission.consent))];
    self.guidanceLabel.stringValue=state.permission.consent==seethis::platform::ChromeConsent::kDenied
        ?@"Allow SeeThis → Google Chrome in Automation, then retry."
        :@"Choose Connect Chrome to request Automation access.";
    self.guidanceLabel.toolTip=text(seethis::platform::ChromeRecoveryGuidance(state));
  }
  [self setNeedsLayout:YES];[self.superview setNeedsLayout:YES];
}
- (CGFloat)placeLabel:(NSTextField*)label width:(CGFloat)width top:(CGFloat)top {
  const CGFloat height=ceil(NSHeight([label.stringValue boundingRectWithSize:
      NSMakeSize(width,CGFLOAT_MAX)
      options:NSStringDrawingUsesLineFragmentOrigin|NSStringDrawingUsesFontLeading
      attributes:@{NSFontAttributeName:label.font}]))+4;
  label.frame=NSMakeRect(16,top,width,height);return top+height+6;
}
- (CGFloat)layoutForWidth:(CGFloat)width {
  const CGFloat available=MAX(1,width-32);
  CGFloat bottom=[self placeLabel:self.shortcutLabel width:available top:2];
  if(self.stateLabel.hidden)return bottom;
  bottom=[self placeLabel:self.stateLabel width:available top:bottom];
  bottom=[self placeLabel:self.guidanceLabel width:available top:bottom];
  CGFloat x=16;
  for(NSButton* button in self.buttons) {
    const CGFloat buttonWidth=MIN(available,ceil(button.fittingSize.width)+8);
    if(x>16 && x+buttonWidth>width-16){x=16;bottom+=34;}
    button.frame=NSMakeRect(x,bottom,buttonWidth,30);x+=buttonWidth+6;
  }
  return bottom+36;
}
- (void)layout {[super layout];[self layoutForWidth:NSWidth(self.bounds)];}
- (void)connectChrome:(id)sender {
  (void)sender;[self.adapter connect];[self refreshChrome];
}
- (void)retryChrome:(id)sender {
  (void)sender;[self.adapter retry];[self refreshChrome];
}
- (void)openAutomationSettings:(id)sender {
  (void)sender;
  if(![self.adapter openSettings]) {
    self.guidanceLabel.stringValue=@"Could not open Settings. Open System Settings > Privacy & Security > Automation > SeeThis > Google Chrome, then choose Retry Chrome.";
    [self setNeedsLayout:YES];[self.superview setNeedsLayout:YES];
  }
}
@end

#if !defined(SEETHIS_INSPECTOR_FEEDBACK_TESTING)
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include "core/reference.h"
#include "service/reference_server.h"

@class STOverlayCoordinator;
@interface STOverlayPanel : NSPanel
@property(nonatomic) seethis::core::DisplayId displayID;
@end
@implementation STOverlayPanel
- (BOOL)canBecomeKeyWindow {return NO;}
- (BOOL)canBecomeMainWindow {return NO;}
@end

@class STInspectorView;
@interface STInspectorPanel : NSPanel
@end
@implementation STInspectorPanel
- (instancetype)initWithContentRect:(NSRect)rect styleMask:(NSWindowStyleMask)style
                            backing:(NSBackingStoreType)backing defer:(BOOL)defer {
  rect.size=NSMakeSize(470,430);
  style&=~(NSWindowStyleMaskResizable|NSWindowStyleMaskMiniaturizable);
  self=[super initWithContentRect:rect styleMask:style backing:backing defer:defer];
  if(self) {
    self.contentMinSize=NSMakeSize(470,430);
    self.contentMaxSize=NSMakeSize(470,430);
    self.collectionBehavior=NSWindowCollectionBehaviorFullScreenNone;
    [self standardWindowButton:NSWindowZoomButton].enabled=NO;
  }
  return self;
}
- (void)setContentSize:(NSSize)size {
  (void)size;[super setContentSize:NSMakeSize(470,430)];
}
- (void)zoom:(id)sender {(void)sender;}
- (void)toggleFullScreen:(id)sender {(void)sender;}
- (BOOL)canBecomeKeyWindow {return NO;}
- (BOOL)canBecomeMainWindow {return NO;}
@end

@interface STOverlayView : NSView
@property(nonatomic,weak) STOverlayCoordinator* coordinator;
@property(nonatomic) seethis::core::DisplayId displayID;
@property(nonatomic) BOOL claimedDeleteSequence;
@end
@interface STOverlayCoordinator : NSObject <NSWindowDelegate> {
  std::unique_ptr<seethis::core::MarkController> _marks;
  seethis::core::ReferenceStore* _references;
  seethis::core::SettingsStore* _settings;
  seethis::service::ReferenceServer* _server;
  seethis::core::DeleteGestureController* _deletion;
  std::string _hoveredID;
  std::string _activeID;
  seethis::platform::InspectorLifecycleState _inspectorLifecycle;
  std::optional<seethis::core::DisplayPoint> _lastPointer;
  std::set<seethis::core::DisplayId> _pendingHoverPresentationDisplays;
  std::uint64_t _hoverGeneration;
  std::uint64_t _presentedHoverGeneration;
  seethis::core::InteractionState _previousState;
  std::uint64_t _generation;
  std::uint64_t _chromeObservationGeneration;
  std::int64_t _originalPID;
  std::optional<seethis::core::Context> _chromeContext;
#if defined(SEETHIS_WINDOW_OBSERVATION_API)
  std::uint64_t _windowObservationGeneration;
  seethis::platform::ChromeObservationLifecycle _chromeLifecycle;
  std::uint64_t _chromePermissionGeneration;
  std::optional<seethis::core::Context> _captureContext;
#endif
  std::vector<seethis::core::FrozenDisplay> _displays;
  __strong id _activationObserver;
  __strong id _escapeMonitor;
  __strong id _localEscapeMonitor;
  __strong STInspectorPanel* _inspectorPanel;
  __strong STInspectorView* _inspectorView;
  __strong NSScrollView* _inspectorScrollView;
  __strong STInspectorChromeView* _inspectorChromeView;
  __strong STInspectorReferenceSelector* _inspectorReferenceSelector;
  __strong NSTimer* _inspectorTimer;
  __strong NSTimer* _chromePageTimer;
#if defined(SEETHIS_WINDOW_OBSERVATION_API)
  __strong NSTimer* _foregroundWindowTimer;
#endif
  __strong NSTimer* _acknowledgementTimer;
  __strong NSString* _acknowledgement;
  __strong NSString* _inspectorActionStatus;
  __strong NSCache<NSString*,NSImage*>* _inspectorThumbnailCache;
  __strong NSMutableSet<NSString*>* _inspectorThumbnailPendingKeys;
  __strong NSMutableSet<NSString*>* _inspectorThumbnailFailedKeys;
  dispatch_queue_t _inspectorThumbnailQueue;
  std::string _acknowledgementID;
  std::string _inspectorAnnotatedReadyID;
  std::string _inspectorAnnotatedReadyDigest;
  std::string _inspectorAnnotatedCopyID;
  bool _inspectorAnnotatedCopyInFlight;
  bool _inspectorCaptureSucceeded;
  bool _inspectorCaptureSelectionPending;
}
@property(nonatomic) seethis::core::InteractionController* controller;
@property(nonatomic,strong) NSMutableArray<STOverlayPanel*>* panels;
- (instancetype)initWithController:(seethis::core::InteractionController*)controller
                         references:(seethis::core::ReferenceStore*)references
                           settings:(seethis::core::SettingsStore*)settings
                             server:(seethis::service::ReferenceServer*)server
                           deletion:(seethis::core::DeleteGestureController*)deletion;
- (void)refresh;
- (void)rebuildDisplays;
- (void)updatePointerPolicy:(const std::optional<seethis::core::DisplayPoint>&)pointer;
- (std::optional<seethis::core::DisplayPoint>)displayPointAtGlobalLogical:(seethis::core::Point2D)global;
- (void)paintDisplay:(seethis::core::DisplayId)display;
- (BOOL)click:(seethis::core::DisplayPoint)point;
- (BOOL)beginRegion:(seethis::core::DisplayPoint)point;
- (void)moveRegion:(seethis::core::DisplayPoint)point;
- (void)endRegion:(seethis::core::DisplayPoint)point;
- (void)abortPending:(NSString*)reason;
- (void)toggleInspector;
- (void)showInspectorIfAllowed:(seethis::core::InspectorSelectionTrigger)trigger;
- (BOOL)inspectorVisible;
- (void)refreshInspector;
- (void)refreshInspectorChromeContext;
- (void)activateInspectorChromeContext:(BOOL)active;
- (void)requestInspectorThumbnail:(std::shared_ptr<const seethis::core::Bytes>)bytes
                        identity:(NSString*)identifier
                          digest:(NSString*)digest
                             key:(NSString*)key;
- (void)prepareInspectorSelection:(const std::string&)identifier;
- (void)selectAcceptedInspectorCapture;
- (void)completeInspectorCapture:(seethis::core::CaptureResult)result
                     generation:(std::uint64_t)generation;
- (BOOL)recopyInspectorSelection:(const std::string&)identifier;
- (void)inspectorSelectReference:(NSString*)identifier;
- (BOOL)referenceCanCopy:(const std::string&)identifier;
- (void)inspectorCopyReference:(id)sender;
- (void)inspectorCopyAnnotated:(id)sender;
- (void)inspectorViewDetails:(id)sender;
- (void)inspectorDeleteSelected:(id)sender;
- (void)showAcknowledgementForID:(const std::string&)identifier;
  - (void)ensureInspectorOnVisibleScreen;
- (void)refreshChromePage;
- (void)stopChromePageRefresh;
#if defined(SEETHIS_WINDOW_OBSERVATION_API)
- (void)refreshForegroundWindow;
- (void)publishForegroundUnavailable;
- (void)invalidateVisibilityPresentation;
#endif
@end

@interface STInspectorView : NSView <STInspectorReferenceLayout>
@property(nonatomic,copy) NSString* text;
@property(nonatomic,copy) NSArray<NSString*>* referenceIDs;
@property(nonatomic,copy) NSArray<NSString*>* referenceRows;
@property(nonatomic,copy) NSString* selectedReferenceID;
@property(nonatomic,copy) NSString* thumbnailKey;
@property(nonatomic,copy) NSString* previewState;
@property(nonatomic,strong) NSImage* thumbnail;
@property(nonatomic) NSRect previewFrame;
@property(nonatomic) BOOL referenceCopyAvailable;
@property(nonatomic) BOOL detailsAvailable;
@property(nonatomic) BOOL annotatedCopyAvailable;
@property(nonatomic) BOOL deleteAvailable;
@property(nonatomic,weak) STOverlayCoordinator* coordinator;
@property(nonatomic,strong) NSButton* referenceCopyButton;
@property(nonatomic,strong) NSButton* detailsButton;
@property(nonatomic,strong) NSButton* annotatedCopyButton;
@property(nonatomic,strong) NSButton* deleteButton;
- (void)fitToWidth:(CGFloat)width;
@end
@implementation STInspectorView
- (BOOL)isOpaque {return YES;}
- (BOOL)isFlipped {return YES;}
- (instancetype)initWithFrame:(NSRect)frame {
  self=[super initWithFrame:frame];
  if(self) {
    _referenceCopyButton=[NSButton buttonWithTitle:@"Copy JSON URL" target:nil
                                              action:@selector(inspectorCopyReference:)];
    _referenceCopyButton.toolTip=@"Copy this reference's JSON URL from any app.";
    _detailsButton=[NSButton buttonWithTitle:@"View details" target:nil
                                    action:@selector(inspectorViewDetails:)];
    _annotatedCopyButton=[NSButton buttonWithTitle:@"Copy marked image" target:nil
                                         action:@selector(inspectorCopyAnnotated:)];
    _annotatedCopyButton.toolTip=@"Copy the full-screen image with marks.";
    _deleteButton=[NSButton buttonWithTitle:@"Delete" target:nil
                                   action:@selector(inspectorDeleteSelected:)];
    for(NSButton* button in @[_referenceCopyButton,_annotatedCopyButton,
                              _detailsButton,_deleteButton]) {
      button.font=[NSFont systemFontOfSize:14 weight:NSFontWeightMedium];
      button.controlSize=NSControlSizeLarge;
    }
    [self addSubview:_referenceCopyButton];
    [self addSubview:_detailsButton];
    [self addSubview:_annotatedCopyButton];
    [self addSubview:_deleteButton];
  }
  return self;
}
- (void)setCoordinator:(STOverlayCoordinator*)coordinator {
  _coordinator=coordinator;
  self.referenceCopyButton.target=coordinator;
  self.detailsButton.target=coordinator;
  self.annotatedCopyButton.target=coordinator;
  self.deleteButton.target=coordinator;
}
- (void)setText:(NSString*)text {
  _text=[text copy];
  [self setNeedsDisplay:YES];
}
- (void)setSelectedReferenceID:(NSString*)identifier {
  if(![_selectedReferenceID isEqualToString:identifier]) {
    _thumbnail=nil;
    _thumbnailKey=nil;
  }
  _selectedReferenceID=[identifier copy];
  [self setNeedsDisplay:YES];
}
- (void)setThumbnailKey:(NSString*)key {
  if(![_thumbnailKey isEqualToString:key])_thumbnail=nil;
  _thumbnailKey=[key copy];
  [self setNeedsDisplay:YES];
}
- (void)fitToWidth:(CGFloat)width {
  NSDictionary* attributes=@{NSFontAttributeName:[NSFont systemFontOfSize:13]};
  const NSRect bounds=[_text boundingRectWithSize:NSMakeSize(MAX(1,width-32),CGFLOAT_MAX)
      options:NSStringDrawingUsesLineFragmentOrigin|NSStringDrawingUsesFontLeading
      attributes:attributes];
  const CGFloat listHeight=4;
  const BOOL selected=self.selectedReferenceID.length>0;
  self.referenceCopyButton.hidden=NO;
  self.detailsButton.hidden=NO;
  self.annotatedCopyButton.hidden=NO;
  self.deleteButton.hidden=NO;
  self.referenceCopyButton.enabled=selected&&self.referenceCopyAvailable;
  self.detailsButton.enabled=selected&&self.detailsAvailable;
  self.annotatedCopyButton.enabled=selected&&self.annotatedCopyAvailable;
  self.deleteButton.enabled=selected&&self.deleteAvailable;
  const CGFloat gap=8;
  const CGFloat available=MAX(1,width-32);
  const CGFloat leftMinimum=ceil(MAX(self.referenceCopyButton.fittingSize.width,
                                     self.detailsButton.fittingSize.width))+16;
  const CGFloat rightMinimum=ceil(MAX(self.annotatedCopyButton.fittingSize.width,
                                      self.deleteButton.fittingSize.width))+16;
  const CGFloat extra=MAX(0,available-leftMinimum-rightMinimum-gap);
  const CGFloat leftWidth=leftMinimum+floor(extra/2);
  const CGFloat rightWidth=available-leftWidth-gap;
  const CGFloat left=16;
  const CGFloat right=left+leftWidth+gap;
  self.referenceCopyButton.frame=NSMakeRect(left,listHeight+4,leftWidth,34);
  self.annotatedCopyButton.frame=NSMakeRect(right,listHeight+4,rightWidth,34);
  self.detailsButton.frame=NSMakeRect(left,listHeight+46,leftWidth,34);
  self.deleteButton.frame=NSMakeRect(right,listHeight+46,rightWidth,34);
  const CGFloat textTop=listHeight+92;
  const CGFloat textBottom=textTop+ceil(NSHeight(bounds));
  const CGFloat previewTop=textBottom+12;
  NSScrollView* scroll=self.enclosingScrollView;
  const CGFloat viewportHeight=scroll?NSHeight(scroll.contentView.bounds):410;
  const CGFloat previewHeight=scroll?MIN(230,MAX(0,viewportHeight-previewTop-20)):230;
  self.previewFrame=NSMakeRect(16,previewTop,available,previewHeight);
  const CGFloat bottom=NSMaxY(self.previewFrame)+20;
  self.frame=NSMakeRect(0,0,width,MAX(viewportHeight,bottom));
}
- (void)drawRect:(NSRect)dirtyRect {
  (void)dirtyRect;
  [NSColor.windowBackgroundColor setFill];NSRectFill(self.bounds);
  NSDictionary* attributes=@{
    NSFontAttributeName:[NSFont systemFontOfSize:13],
    NSForegroundColorAttributeName:NSColor.labelColor};
  const CGFloat listHeight=4;
  NSString* text=self.text ? self.text : @"No reference selected";
  const CGFloat textTop=listHeight+92;
  [text drawInRect:NSMakeRect(16,textTop,NSWidth(self.bounds)-32,NSHeight(self.bounds)-textTop-10)
      withAttributes:attributes];
  if(!NSIsEmptyRect(self.previewFrame)) {
    [[NSColor.separatorColor colorWithAlphaComponent:0.7] setStroke];
    [[NSBezierPath bezierPathWithRoundedRect:self.previewFrame xRadius:8 yRadius:8] stroke];
    if(self.thumbnail) {
      const NSSize size=self.thumbnail.size;
      const CGFloat scale=MIN(NSWidth(self.previewFrame)/size.width,
                              NSHeight(self.previewFrame)/size.height);
      const NSSize fitted=NSMakeSize(size.width*scale,size.height*scale);
      const NSRect imageFrame=NSMakeRect(
          NSMidX(self.previewFrame)-fitted.width/2,
          NSMidY(self.previewFrame)-fitted.height/2,
          fitted.width,fitted.height);
      [self.thumbnail drawInRect:imageFrame fromRect:NSZeroRect
                     operation:NSCompositingOperationSourceOver fraction:1
                respectFlipped:YES hints:nil];
    } else {
      NSString* state=self.previewState.length?self.previewState:@"Preview unavailable.";
      const NSSize size=[state sizeWithAttributes:attributes];
      [state drawAtPoint:NSMakePoint(NSMidX(self.previewFrame)-size.width/2,
                                   NSMidY(self.previewFrame)-size.height/2)
          withAttributes:attributes];
    }
  }
}
@end
@implementation STOverlayView
- (BOOL)isOpaque {return NO;}
- (BOOL)acceptsFirstMouse:(NSEvent*)event {(void)event;return YES;}
- (BOOL)needsPanelToBecomeKey {return NO;}
- (BOOL)shouldDelayWindowOrderingForEvent:(NSEvent*)event {(void)event;return YES;}
- (BOOL)mouseDownCanMoveWindow {return NO;}
- (void)drawRect:(NSRect)dirtyRect {(void)dirtyRect;[self.coordinator paintDisplay:self.displayID];}
- (void)mouseDown:(NSEvent*)event {
  NSPoint local=[self convertPoint:event.locationInWindow fromView:nil];
  const seethis::core::DisplayPoint point={self.displayID,
    seethis::core::CoordinateUnit::kLogicalPoints,{local.x,local.y},
    self.window.backingScaleFactor};
  self.claimedDeleteSequence=[self.coordinator beginRegion:point]||
      [self.coordinator click:point];
}
- (void)mouseDragged:(NSEvent*)event {
  NSPoint local=[self convertPoint:event.locationInWindow fromView:nil];
  [self.coordinator moveRegion:{self.displayID,
    seethis::core::CoordinateUnit::kLogicalPoints,{local.x,local.y},
    self.window.backingScaleFactor}];
}
- (void)mouseUp:(NSEvent*)event {
  NSPoint local=[self convertPoint:event.locationInWindow fromView:nil];
  [self.coordinator endRegion:{self.displayID,
    seethis::core::CoordinateUnit::kLogicalPoints,{local.x,local.y},
    self.window.backingScaleFactor}];
  if(self.claimedDeleteSequence)self.claimedDeleteSequence=NO;
}
@end

namespace {
void Stroke(const std::vector<seethis::core::DisplayPoint>& samples,
            seethis::core::DisplayId display,NSColor* color,
            CGFloat width=4) {
  NSBezierPath* path=[NSBezierPath bezierPath];path.lineWidth=width;
  path.lineCapStyle=NSLineCapStyleRound;path.lineJoinStyle=NSLineJoinStyleRound;
  bool connected=false;
  for(const auto& p:samples) {
    if(p.display_id!=display){connected=false;continue;}
    NSPoint local=NSMakePoint(p.position.x,p.position.y);
    if(connected)[path lineToPoint:local];else [path moveToPoint:local];connected=true;
  }
  [color setStroke];[path stroke];
}
void StrokeRegions(const std::vector<seethis::core::ReferenceRegion>& regions,
                   seethis::core::DisplayId display,NSColor* color,
                   CGFloat width=4) {
  for(const auto& region:regions)
    seethis::core::ForEachSubpath(region, [&](const auto& path) {
      Stroke(path,display,color,width);
    });
}
void Label(NSString* text,NSPoint point) {
  [text drawAtPoint:point withAttributes:@{NSFontAttributeName:[NSFont systemFontOfSize:12 weight:NSFontWeightSemibold],
    NSForegroundColorAttributeName:NSColor.whiteColor,NSBackgroundColorAttributeName:[NSColor colorWithWhite:0 alpha:0.8]}];
}
NSColor* MarkColor(bool selected) {
  return selected?[NSColor colorWithSRGBRed:0.2 green:0.72 blue:1 alpha:0.96]:
      [NSColor colorWithSRGBRed:1 green:0.48 blue:0.08 alpha:0.92];
}
NSRect StrokeBounds(const std::vector<seethis::core::DisplayPoint>& samples,
                    seethis::core::DisplayId display) {
  const auto bounds=seethis::platform::StrokeDirtyBounds(samples,display,6);
  return bounds?NSMakeRect(bounds->x,bounds->y,bounds->width,bounds->height):
                NSZeroRect;
}
NSRect RegionBounds(const std::vector<seethis::core::ReferenceRegion>& regions,
                    seethis::core::DisplayId display) {
  NSRect result=NSZeroRect;
  for(const auto& region:regions) {
    seethis::core::ForEachSubpath(region, [&](const auto& path) {
      const auto bounds=StrokeBounds(path,display);
      if(!NSIsEmptyRect(bounds))result=NSIsEmptyRect(result)?bounds:NSUnionRect(result,bounds);
    });
  }
  return result;
}
const char* DeleteResultName(seethis::core::DeleteResult result) {
  switch(result) {
    case seethis::core::DeleteResult::kDeleted:return "deleted";
    case seethis::core::DeleteResult::kAlreadyDeleted:return "already-deleted";
    case seethis::core::DeleteResult::kNotFound:return "not-found";
    case seethis::core::DeleteResult::kCleanupFailed:return "cleanup-failed";
  }
  return "unknown";
}
const char* JobStateName(seethis::core::ReferenceJobState state) {
  switch(state) {
    case seethis::core::ReferenceJobState::kPending:return "pending";
    case seethis::core::ReferenceJobState::kIndexing:return "indexing";
    case seethis::core::ReferenceJobState::kReady:return "ready";
    case seethis::core::ReferenceJobState::kFailed:return "failed";
    case seethis::core::ReferenceJobState::kExpired:return "expired";
    case seethis::core::ReferenceJobState::kDeleted:return "deleted";
  }
  return "unknown";
}
NSString* TimestampText(std::int64_t utc_us) {
  if(utc_us<=0)return @"—";
  NSDate* date=[NSDate dateWithTimeIntervalSince1970:utc_us/1000000.0];
  return [NSDateFormatter localizedStringFromDate:date
      dateStyle:NSDateFormatterShortStyle timeStyle:NSDateFormatterMediumStyle];
}
NSString* InspectorReferenceName(const seethis::core::ReferenceJobSnapshot& job) {
  const auto& context=job.context;
  for(const std::string* name:{context.bundle_id!="com.google.Chrome"?&context.window_title:nullptr,
                             &context.app_name,&context.bundle_id}) {
    if(!name)continue;
    NSString* text=[NSString stringWithUTF8String:name->c_str()];
    NSMutableArray<NSString*>* words=[NSMutableArray array];
    for(NSString* word in [text componentsSeparatedByCharactersInSet:
        NSCharacterSet.whitespaceAndNewlineCharacterSet])if(word.length)[words addObject:word];
    if(words.count)return [words componentsJoinedByString:@" "];
  }
  return @"Reference";
}
NSString* InspectorReferenceRow(NSString* name,NSString* time) {
  NSDictionary* attributes=@{NSFontAttributeName:[NSFont systemFontOfSize:12]};
  NSString* suffix=[@" — " stringByAppendingString:time];
  if([[name stringByAppendingString:suffix] sizeWithAttributes:attributes].width>430) {
    NSMutableString* shortened=[NSMutableString string];
    for(NSUInteger offset=0;offset<name.length;) {
      NSRange glyph=[name rangeOfComposedCharacterSequenceAtIndex:offset];
      NSString* next=[shortened stringByAppendingString:[name substringWithRange:glyph]];
      if([[next stringByAppendingFormat:@"…%@",suffix] sizeWithAttributes:attributes].width>430)break;
      [shortened setString:next];offset=NSMaxRange(glyph);
    }
    name=[shortened stringByAppendingString:@"…"];
  }
  return [name stringByAppendingString:suffix];
}
#if defined(SEETHIS_WINDOW_OBSERVATION_API)
bool SameWindowIdentity(const seethis::core::WindowObservation& first,
                        const seethis::core::WindowObservation& second) {
  return first.availability==second.availability && first.pid==second.pid &&
      first.bundle_id==second.bundle_id &&
      first.window_id==second.window_id && first.bounds.x==second.bounds.x &&
      first.bounds.y==second.bounds.y &&
      first.bounds.width==second.bounds.width &&
      first.bounds.height==second.bounds.height;
}
#endif
}
@implementation STOverlayCoordinator
- (instancetype)initWithController:(seethis::core::InteractionController*)controller
                         references:(seethis::core::ReferenceStore*)references
                           settings:(seethis::core::SettingsStore*)settings
                             server:(seethis::service::ReferenceServer*)server
                           deletion:(seethis::core::DeleteGestureController*)deletion {
  self=[super init];if(self) {
    _controller=controller;_panels=[NSMutableArray array];_previousState=seethis::core::InteractionState::kIdle;
    _references=references;_settings=settings;_server=server;_deletion=deletion;
    _inspectorAnnotatedCopyInFlight=false;
    _inspectorThumbnailCache=[NSCache new];
    _inspectorThumbnailCache.countLimit=8;
    _inspectorThumbnailCache.totalCostLimit=32*1024*1024;
    _inspectorThumbnailPendingKeys=[NSMutableSet set];
    _inspectorThumbnailFailedKeys=[NSMutableSet set];
    _inspectorThumbnailQueue=dispatch_queue_create(
        "local.seethis.inspector-thumbnail",DISPATCH_QUEUE_SERIAL);
    _marks=std::make_unique<seethis::core::MarkController>(
      *references,
      [server](std::string_view id)->std::optional<std::int64_t> {
        if(server->port()==0)return {};
        return seethis::platform::CopyReferenceText(server->ClipboardText(id));
      },
      [server](std::shared_ptr<const seethis::core::StoredReference> stored,
               std::int64_t expected) {
        (void)server;(void)stored;(void)expected;
        return true;
      },
      [](std::function<void()> task) {
        auto owned=std::make_shared<std::function<void()>>(std::move(task));
        dispatch_async(dispatch_get_main_queue(),^{(*owned)();});
      });
    _inspectorPanel=[[STInspectorPanel alloc]
        initWithContentRect:NSMakeRect(120,120,470,430)
        styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|
          NSWindowStyleMaskNonactivatingPanel
        backing:NSBackingStoreBuffered defer:NO];
    _inspectorPanel.title=@"SeeThis Inspector";
    _inspectorPanel.level=NSFloatingWindowLevel;
    _inspectorPanel.floatingPanel=YES;
    _inspectorPanel.hidesOnDeactivate=NO;
    _inspectorPanel.movableByWindowBackground=YES;
    _inspectorPanel.releasedWhenClosed=NO;
    _inspectorPanel.ignoresMouseEvents=YES;
    _inspectorPanel.delegate=self;
    _inspectorView=[[STInspectorView alloc]
        initWithFrame:_inspectorPanel.contentView.bounds];
    _inspectorView.coordinator=self;
    _inspectorView.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;
    _inspectorView.text=@"No reference selected\n\nSelect a circle to inspect its metadata.";
    _inspectorScrollView=[[NSScrollView alloc]
        initWithFrame:_inspectorPanel.contentView.bounds];
    _inspectorScrollView.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;
    _inspectorScrollView.hasVerticalScroller=seethis::core::InspectorMetadataBodyScrollable();
    _inspectorScrollView.hasHorizontalScroller=NO;
    _inspectorScrollView.autohidesScrollers=YES;
    _inspectorScrollView.borderType=NSNoBorder;
    _inspectorScrollView.drawsBackground=NO;
    _inspectorScrollView.documentView=_inspectorView;
    STInspectorContainerView* inspectorContainer=[[STInspectorContainerView alloc]
        initWithFrame:_inspectorPanel.contentView.bounds];
    inspectorContainer.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;
    inspectorContainer.feedbackView=[[STInspectorFeedbackView alloc]
        initWithAdapter:[STSystemInspectorFeedbackAdapter new]];
    inspectorContainer.referenceScrollView=_inspectorScrollView;
    _inspectorChromeView=[[STInspectorChromeView alloc]
        initWithFrame:NSMakeRect(0,0,NSWidth(inspectorContainer.bounds),125)];
    inspectorContainer.chromeView=_inspectorChromeView;
    _inspectorReferenceSelector=[[STInspectorReferenceSelector alloc] initWithFrame:NSZeroRect];
    _inspectorReferenceSelector.target=(id<STInspectorReferenceTarget>)self;
    inspectorContainer.referenceSelector=_inspectorReferenceSelector;
    [inspectorContainer addSubview:inspectorContainer.feedbackView];
    [inspectorContainer addSubview:_inspectorChromeView];
    [inspectorContainer addSubview:_inspectorReferenceSelector];
    [inspectorContainer addSubview:_inspectorScrollView];
    _inspectorPanel.contentView=inspectorContainer;
    [inspectorContainer setNeedsLayout:YES];
    [inspectorContainer layoutSubtreeIfNeeded];
    __weak STOverlayCoordinator* weakInspectorSelf=self;
    _inspectorTimer=[NSTimer scheduledTimerWithTimeInterval:0.5 repeats:YES
      block:^(NSTimer*) {
        STOverlayCoordinator* owner=weakInspectorSelf;
        if(owner && owner->_inspectorPanel.isVisible)[owner refreshInspector];
      }];
    [self rebuildDisplays];
    __weak STOverlayCoordinator* weakSelf=self;
#if defined(SEETHIS_WINDOW_OBSERVATION_API)
    _foregroundWindowTimer=[NSTimer scheduledTimerWithTimeInterval:0.25
      repeats:YES block:^(NSTimer*) {
        STOverlayCoordinator* owner=weakSelf;
        if(owner)[owner refreshForegroundWindow];
      }];
    [self refreshForegroundWindow];
#endif
    _activationObserver=[NSWorkspace.sharedWorkspace.notificationCenter addObserverForName:NSWorkspaceDidActivateApplicationNotification
      object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification*) {
        STOverlayCoordinator* owner=weakSelf;
        if(!owner)return;
#if defined(SEETHIS_WINDOW_OBSERVATION_API)
        // Activation is a hard boundary: hide and disable the old foreground
        // projection before any delayed window-list/provider work can run.
        [owner publishForegroundUnavailable];
        dispatch_async(dispatch_get_main_queue(),^{
          STOverlayCoordinator* refreshed=weakSelf;
          if(refreshed)[refreshed refreshForegroundWindow];
        });
#else
        // Invalidate the last ready page synchronously with app activation so
        // a hotkey or click cannot act during the provider refresh interval.
        if(owner->_chromeContext) {
          const auto generation=++owner->_chromeObservationGeneration;
          owner->_marks->SetPageObservation({
              seethis::core::PageAvailability::kUnavailable,generation,
              seethis::core::Now(),std::nullopt});
          for(STOverlayPanel* panel in owner.panels)
            [panel.contentView setNeedsDisplay:YES];
          [owner updatePointerPolicy:owner->_lastPointer];
        }
        if(owner->_marks->Pending() && owner->_originalPID!=NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier)
          [owner abortPending:@"foreground application changed"];
#endif
        [owner activateInspectorChromeContext:
            [NSWorkspace.sharedWorkspace.frontmostApplication.bundleIdentifier
                isEqualToString:@"com.google.Chrome"]];
      }];
    // InputAdapter only cancels while drawing; Escape must also abort an already
    // released transaction awaiting pixels. These monitors never swallow events.
    _escapeMonitor=[NSEvent addGlobalMonitorForEventsMatchingMask:NSEventMaskKeyDown handler:^(NSEvent* event) {
      if(event.keyCode==53)[weakSelf abortPending:@"Escape"];
    }];
    _localEscapeMonitor=[NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown handler:^NSEvent*(NSEvent* event) {
      if(event.keyCode==53)[weakSelf abortPending:@"Escape"];return event;
    }];
  }return self;
}
- (void)dealloc {
  [_inspectorTimer invalidate];_inspectorTimer=nil;
#if defined(SEETHIS_WINDOW_OBSERVATION_API)
  [_foregroundWindowTimer invalidate];_foregroundWindowTimer=nil;
#endif
  [self stopChromePageRefresh];
  [_acknowledgementTimer invalidate];_acknowledgementTimer=nil;
  [_inspectorPanel orderOut:nil];_inspectorPanel.delegate=nil;
  if(_activationObserver)[NSWorkspace.sharedWorkspace.notificationCenter removeObserver:_activationObserver];
  if(_escapeMonitor)[NSEvent removeMonitor:_escapeMonitor];
  if(_localEscapeMonitor)[NSEvent removeMonitor:_localEscapeMonitor];
  for(STOverlayPanel* panel in self.panels)[panel close];
}
- (BOOL)inspectorVisible {return _inspectorPanel!=nil&&_inspectorPanel.isVisible;}
- (void)windowWillClose:(NSNotification*)notification {
  if(notification.object==_inspectorPanel) {
    _inspectorLifecycle=seethis::platform::ReduceInspectorLifecycle(
        _inspectorLifecycle,
        seethis::platform::InspectorLifecycleEvent::kUserClose);
    _inspectorPanel.ignoresMouseEvents=YES;
  }
}
- (BOOL)referenceCanCopy:(const std::string&)identifier {
  if(identifier.empty() || !_server || _server->port()==0 ||
     (_deletion && _deletion->Snapshot().blocks_mark_copy()))return NO;
  const auto job=_references->LookupMetadata(identifier);
  return job && job->state==seethis::core::ReferenceJobState::kReady;
}
- (void)requestInspectorThumbnail:(std::shared_ptr<const seethis::core::Bytes>)bytes
                        identity:(NSString*)identifier
                          digest:(NSString*)digest
                             key:(NSString*)key {
  if(!bytes || bytes->empty() || [_inspectorThumbnailPendingKeys containsObject:key])return;
  [_inspectorThumbnailPendingKeys addObject:key];
  const auto presentationGeneration=_inspectorLifecycle.selection_generation;
  __weak STOverlayCoordinator* weakOwner=self;
  dispatch_async(_inspectorThumbnailQueue,^{
    CGImageRef thumbnail=nullptr;
    @autoreleasepool {
      NSData* png=[NSData dataWithBytes:bytes->data() length:bytes->size()];
      NSBitmapImageRep* decoded=[[NSBitmapImageRep alloc] initWithData:png];
      CGImageRef source=decoded.CGImage;
      if(source && CGImageGetWidth(source)>0 && CGImageGetHeight(source)>0) {
        const size_t sourceWidth=CGImageGetWidth(source);
        const size_t sourceHeight=CGImageGetHeight(source);
        const double factor=MIN(1.0,1024.0/static_cast<double>(
            MAX(sourceWidth,sourceHeight)));
        const size_t width=MAX(1,static_cast<size_t>(llround(sourceWidth*factor)));
        const size_t height=MAX(1,static_cast<size_t>(llround(sourceHeight*factor)));
        CGColorSpaceRef color=CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        const auto bitmapInfo=static_cast<CGBitmapInfo>(
            static_cast<unsigned>(kCGImageAlphaPremultipliedLast)|
            static_cast<unsigned>(kCGBitmapByteOrder32Big));
        CGContextRef bitmap=CGBitmapContextCreate(nullptr,width,height,8,width*4,
            color,bitmapInfo);
        if(bitmap) {
          CGContextSetInterpolationQuality(bitmap,kCGInterpolationHigh);
          CGContextDrawImage(bitmap,CGRectMake(0,0,width,height),source);
          thumbnail=CGBitmapContextCreateImage(bitmap);
          CGContextRelease(bitmap);
        }
        if(color)CGColorSpaceRelease(color);
      }
    }
    dispatch_async(dispatch_get_main_queue(),^{
      STOverlayCoordinator* owner=weakOwner;
      if(owner) {
        [owner->_inspectorThumbnailPendingKeys removeObject:key];
        const std::string selected=identifier.UTF8String;
        const auto current=owner->_references->LookupMetadata(selected);
        const BOOL ready=current &&
            current->state==seethis::core::ReferenceJobState::kReady;
        NSImage* image=nil;
        if(thumbnail && ready)
          image=[[NSImage alloc] initWithCGImage:thumbnail
              size:NSMakeSize(CGImageGetWidth(thumbnail),CGImageGetHeight(thumbnail))];
        if(image) {
          const NSUInteger cost=CGImageGetWidth(thumbnail)*CGImageGetHeight(thumbnail)*4;
          [owner->_inspectorThumbnailCache setObject:image forKey:key cost:cost];
          if(seethis::platform::InspectorPresentationIsCurrent(
                 owner->_inspectorLifecycle,selected,presentationGeneration) &&
             owner->_inspectorAnnotatedReadyID==selected &&
             owner->_inspectorAnnotatedReadyDigest==digest.UTF8String &&
             [owner->_inspectorView.thumbnailKey isEqualToString:key]) {
            owner->_inspectorView.thumbnail=image;
            owner->_inspectorView.previewState=nil;
            [owner->_inspectorView setNeedsDisplay:YES];
          }
        } else if(ready && seethis::platform::InspectorPresentationIsCurrent(
                     owner->_inspectorLifecycle,selected,presentationGeneration)) {
          if(owner->_inspectorThumbnailFailedKeys.count>=32)
            [owner->_inspectorThumbnailFailedKeys removeAllObjects];
          [owner->_inspectorThumbnailFailedKeys addObject:key];
          if([owner->_inspectorView.thumbnailKey isEqualToString:key]) {
            owner->_inspectorView.previewState=@"Preview unavailable.";
            [owner->_inspectorView setNeedsDisplay:YES];
          }
        }
      }
      if(thumbnail)CGImageRelease(thumbnail);
    });
  });
}
- (void)activateInspectorChromeContext:(BOOL)active {
  _inspectorChromeView.chromeContextActive=active;
  [self refreshInspectorChromeContext];
}
- (void)refreshInspectorChromeContext {
  [_inspectorChromeView refreshChrome];
  [_inspectorChromeView.superview layoutSubtreeIfNeeded];
}
- (void)refreshInspector {
  if(!_inspectorView)return;
  [self refreshInspectorChromeContext];
  NSMutableArray<NSString*>* ids=[NSMutableArray array];
  NSMutableArray<NSString*>* rows=[NSMutableArray array];
  NSMutableArray<NSString*>* times=[NSMutableArray array];
  for(const auto& item:_marks->inspector_jobs()) {
    [ids addObject:[NSString stringWithUTF8String:item.id.c_str()]];
    NSString* time=TimestampText(item.accepted_utc_us);
    [times addObject:time];
    [rows addObject:InspectorReferenceRow(InspectorReferenceName(item),time)];
  }
  _inspectorView.referenceIDs=ids;
  _inspectorView.referenceRows=rows;
  NSString* selectedID=[NSString stringWithUTF8String:
      _inspectorLifecycle.selected_id.c_str()];
  _inspectorView.selectedReferenceID=selectedID;
  _inspectorReferenceSelector.referenceIDs=ids;
  _inspectorReferenceSelector.referenceRows=rows;
  _inspectorReferenceSelector.referenceTimes=times;
  _inspectorReferenceSelector.selectedReferenceID=selectedID;
  [_inspectorReferenceSelector refreshMenu];
  const auto selected_job=_inspectorLifecycle.selected_id.empty()
      ? std::optional<seethis::core::ReferenceJobSnapshot>{}
      : _references->LookupMetadata(_inspectorLifecycle.selected_id);
  _inspectorView.referenceCopyAvailable=
      [self referenceCanCopy:_inspectorLifecycle.selected_id];
  _inspectorView.detailsAvailable=selected_job &&
      (selected_job->state==seethis::core::ReferenceJobState::kReady ||
       selected_job->state==seethis::core::ReferenceJobState::kExpired) &&
      _server->port()!=0;
  _inspectorView.deleteAvailable=selected_job &&
      selected_job->state!=seethis::core::ReferenceJobState::kDeleted;
  _inspectorView.annotatedCopyAvailable=NO;
  _inspectorAnnotatedReadyID.clear();
  _inspectorAnnotatedReadyDigest.clear();
  NSString* thumbnailKey=[selectedID stringByAppendingString:@":unavailable"];
  NSString* previewState=@"Preview unavailable.";
  NSImage* cachedThumbnail=nil;
  std::shared_ptr<const seethis::core::Bytes> thumbnailBytes;
  NSString* thumbnailDigest=nil;
  if(selected_job && selected_job->state==seethis::core::ReferenceJobState::kReady) {
    // Derivation is scheduled by the store. Only the explicit button may write
    // an image to the pasteboard.
    const auto asset=_references->RequestReadyAnnotated(_inspectorLifecycle.selected_id);
    if(asset.state==seethis::core::ReadyAssetState::kReady &&
       asset.bytes && !asset.bytes->empty() && !asset.byte_sha256.empty()) {
      _inspectorAnnotatedReadyID=_inspectorLifecycle.selected_id;
      _inspectorAnnotatedReadyDigest=asset.byte_sha256;
      _inspectorView.annotatedCopyAvailable=!_inspectorAnnotatedCopyInFlight;
      NSString* digest=[NSString stringWithUTF8String:asset.byte_sha256.c_str()];
      NSString* key=[NSString stringWithFormat:@"%@:%@",selectedID,digest];
      thumbnailKey=key;
      cachedThumbnail=[_inspectorThumbnailCache objectForKey:key];
      if(!cachedThumbnail && [_inspectorView.thumbnailKey isEqualToString:key])
        cachedThumbnail=_inspectorView.thumbnail;
      if(cachedThumbnail) {
        previewState=nil;
      } else if([_inspectorThumbnailFailedKeys containsObject:key]) {
        previewState=@"Preview unavailable.";
      } else {
        previewState=@"Preparing preview…";
        thumbnailBytes=asset.bytes;
        thumbnailDigest=digest;
      }
    } else if(asset.state==seethis::core::ReadyAssetState::kPending) {
      thumbnailKey=[selectedID stringByAppendingString:@":pending"];
      previewState=@"Preparing image…";
    }
  } else if(selected_job &&
            (selected_job->state==seethis::core::ReferenceJobState::kPending ||
             selected_job->state==seethis::core::ReferenceJobState::kIndexing)) {
    thumbnailKey=[selectedID stringByAppendingString:@":pending"];
    previewState=@"Preparing reference…";
  }
  _inspectorView.thumbnailKey=thumbnailKey;
  _inspectorView.previewState=previewState;
  if(cachedThumbnail)_inspectorView.thumbnail=cachedThumbnail;
  if(thumbnailBytes)
    [self requestInspectorThumbnail:thumbnailBytes identity:selectedID
                            digest:thumbnailDigest key:thumbnailKey];
  if(_inspectorLifecycle.selected_id.empty()) {
    _inspectorView.text=@"Select a reference above to preview it.";
    [self layoutInspectorText];return;
  }
  const auto& job=selected_job;
  if(!job) {
    _inspectorView.text=@"Reference unavailable.";
    [self layoutInspectorText];return;
  }
  _inspectorView.text=@"Selected reference";
  [self layoutInspectorText];
}
- (void)layoutInspectorText {
  if(!_inspectorView||!_inspectorScrollView)return;
  if(_inspectorActionStatus.length)
    _inspectorView.text=[_inspectorView.text stringByAppendingFormat:
        @"\n\n%@",_inspectorActionStatus];
  [_inspectorView fitToWidth:NSWidth(_inspectorScrollView.contentView.bounds)];
  [_inspectorView setNeedsDisplay:YES];
}
- (void)prepareInspectorSelection:(const std::string&)identifier {
  if(!_inspectorView)return;
  NSString* selected=[NSString stringWithUTF8String:identifier.c_str()];
  if([_inspectorView.selectedReferenceID isEqualToString:selected])return;
  _inspectorView.selectedReferenceID=selected;
  _inspectorView.thumbnailKey=nil;
  _inspectorView.previewState=@"Preparing image…";
}
- (void)completeInspectorCapture:(seethis::core::CaptureResult)result
                     generation:(std::uint64_t)generation {
  if(generation==_generation) {
    _inspectorCaptureSucceeded=result.error.empty() && seethis::core::ValidPixels(result.pixels);
    if(!_inspectorCaptureSucceeded)_inspectorCaptureSelectionPending=false;
  }
  _marks->Captured(generation,std::move(result));
  if(generation==_generation)[self selectAcceptedInspectorCapture];
}
- (void)selectAcceptedInspectorCapture {
  if(!_inspectorCaptureSelectionPending || !_inspectorCaptureSucceeded || _activeID.empty())return;
  const auto next=seethis::platform::InspectorAfterCapture(
      _inspectorLifecycle,_activeID,_marks->inspector_jobs());
  if(next.selection_generation==_inspectorLifecycle.selection_generation)return;
  _inspectorLifecycle=next;
  _inspectorCaptureSelectionPending=false;
  _inspectorActionStatus=nil;
  [self prepareInspectorSelection:_activeID];
  if(_marks->copy_result()==seethis::core::CopyResult::kCopied) {
    _inspectorActionStatus=@"JSON URL copied.";
    [self showAcknowledgementForID:_activeID];
  } else if(_marks->copy_result()==seethis::core::CopyResult::kFailed ||
            _marks->copy_result()==seethis::core::CopyResult::kPersistedClipboardFailed) {
    _inspectorActionStatus=@"JSON URL was not copied.";
  }
  [self showInspectorIfAllowed:seethis::core::InspectorSelectionTrigger::kNewCircle];
  [self refreshInspector];
}
- (BOOL)recopyInspectorSelection:(const std::string&)identifier {
  BOOL copied=NO;
  try {copied=_marks->RecopyJob(identifier);}
  catch(...) {}
  _inspectorActionStatus=copied?@"JSON URL copied.":@"JSON URL was not copied.";
  if(copied)[self showAcknowledgementForID:identifier];
  return copied;
}
- (void)inspectorSelectReference:(NSString*)identifier {
  if(identifier.length==0)return;
  if(!seethis::platform::InspectorReferenceAvailable(
         _marks->inspector_jobs(),identifier.UTF8String)) {
    _inspectorActionStatus=@"Reference unavailable; selection was not changed.";
    [self refreshInspector];return;
  }
  _inspectorActionStatus=nil;
  const std::string previousID=_inspectorLifecycle.selected_id;
  _inspectorLifecycle=seethis::platform::ReduceInspectorLifecycle(
      _inspectorLifecycle,
      seethis::platform::InspectorLifecycleEvent::kReferenceSelection,
      identifier.UTF8String);
  [self prepareInspectorSelection:_inspectorLifecycle.selected_id];
  if(seethis::platform::InspectorSelectionChangesPresentation(
         previousID,_inspectorLifecycle.selected_id))
    for(STOverlayPanel* panel in self.panels)[panel.contentView setNeedsDisplay:YES];
  [self recopyInspectorSelection:_inspectorLifecycle.selected_id];
  [self refreshInspector];
  [self updatePointerPolicy:_lastPointer];
}
- (void)inspectorCopyReference:(id)sender {
  (void)sender;
  const std::string selected=_inspectorLifecycle.selected_id;
  if(selected.empty() ||
     ![_inspectorView.selectedReferenceID isEqualToString:
         [NSString stringWithUTF8String:selected.c_str()]])return;
  // This explicit history action uses the persisted ready reference and is
  // independent of the live source-window visibility required by mark recopy.
  if(![self referenceCanCopy:selected]) {
    _inspectorActionStatus=@"Reference is no longer ready; JSON URL was not copied.";
    [self refreshInspector];return;
  }
  BOOL copied=NO;
  try {copied=_marks->CopyHistoryJob(selected);}
  catch(...) {}
  if(copied) {
    _inspectorActionStatus=@"Reference JSON URL copied.";
    [self showAcknowledgementForID:selected];
  } else {
    _inspectorActionStatus=@"Reference JSON URL could not be copied.";
  }
  [self refreshInspector];
}
- (void)inspectorViewDetails:(id)sender {
  (void)sender;
  const std::string selected=_inspectorLifecycle.selected_id;
  if(selected.empty() ||
     ![_inspectorView.selectedReferenceID isEqualToString:
         [NSString stringWithUTF8String:selected.c_str()]])return;
  const auto job=_references->LookupMetadata(selected);
  if(!job || (job->state!=seethis::core::ReferenceJobState::kReady &&
              job->state!=seethis::core::ReferenceJobState::kExpired) ||
     _server->port()==0) {
    _inspectorActionStatus=@"Details unavailable for this reference.";
    [self refreshInspector];return;
  }
  // The existing viewer checks its own capability and returns 410 for expiry.
  // This explicit history read never touches the pasteboard or live page gate.
  BOOL opened=NO;
  try {
    const std::string url=_server->ViewerUrl(selected);
    NSURL* viewer=[NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]];
    if(viewer)opened=[NSWorkspace.sharedWorkspace openURL:viewer];
  } catch(...) {}
  _inspectorActionStatus=opened?nil:@"Could not open the reference viewer.";
  [self refreshInspector];
}
- (void)inspectorCopyAnnotated:(id)sender {
  (void)sender;
  const std::string selected=_inspectorLifecycle.selected_id;
  if(_inspectorAnnotatedCopyInFlight || selected.empty() ||
     ![_inspectorView.selectedReferenceID isEqualToString:
         [NSString stringWithUTF8String:selected.c_str()]] ||
     !_inspectorView.annotatedCopyAvailable ||
     selected!=_inspectorAnnotatedReadyID) {
    _inspectorActionStatus=@"Marked image unavailable for the current selection.";
    [self refreshInspector];return;
  }
  const auto before=_references->LookupMetadata(selected);
  if(!before || before->state!=seethis::core::ReferenceJobState::kReady) {
    _inspectorActionStatus=@"Reference is no longer ready; image was not copied.";
    [self refreshInspector];return;
  }
  const auto asset=_references->RequestReadyAnnotated(selected);
  const auto after=_references->LookupMetadata(selected);
  if(asset.state!=seethis::core::ReadyAssetState::kReady ||
     !asset.bytes || asset.bytes->empty() ||
     asset.byte_sha256.empty() ||
     asset.byte_sha256!=_inspectorAnnotatedReadyDigest ||
     !after || after->state!=seethis::core::ReferenceJobState::kReady ||
     _inspectorLifecycle.selected_id!=selected) {
    _inspectorActionStatus=@"Marked image changed or is unavailable; image was not copied.";
    [self refreshInspector];return;
  }
  _inspectorAnnotatedCopyInFlight=true;
  _inspectorAnnotatedCopyID=selected;
  _inspectorActionStatus=@"Preparing marked image for the pasteboard…";
  [self refreshInspector];
  const auto started=std::chrono::steady_clock::now();
  const auto presentationGeneration=_inspectorLifecycle.selection_generation;
  __weak STOverlayCoordinator* weakOwner=self;
  // The helper validates and converts off the UI thread. Its deadline prevents
  // late publication; decode/encode may continue after a timeout.
  seethis::platform::CopyAnnotatedPng(asset.bytes,std::chrono::milliseconds(5000),
      [weakOwner,selected,started,presentationGeneration](
          seethis::platform::AnnotatedCopyResult result) {
        STOverlayCoordinator* owner=weakOwner;
        if(!owner || owner->_inspectorAnnotatedCopyID!=selected)return;
        owner->_inspectorAnnotatedCopyInFlight=false;
        owner->_inspectorAnnotatedCopyID.clear();
        if(!seethis::platform::InspectorPresentationIsCurrent(
               owner->_inspectorLifecycle,selected,presentationGeneration)) {
          [owner refreshInspector];return;
        }
        const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now()-started).count();
        std::ostringstream status;
        switch(result.status) {
          case seethis::platform::AnnotatedCopyStatus::kCopied:
            status<<"Marked image copied: "<<result.width<<"×"<<result.height
                  <<" pixels";
            if(result.downscaled)status<<"; downsampled for paste";
            status<<".";
            break;
          case seethis::platform::AnnotatedCopyStatus::kInvalidPng:
            status<<"Marked image PNG was invalid; nothing was copied.";break;
          case seethis::platform::AnnotatedCopyStatus::kTooLarge:
            status<<"Marked image exceeds the safe size limit; nothing was copied.";break;
          case seethis::platform::AnnotatedCopyStatus::kTimedOut:
            status<<"Marked image copy timed out after "<<elapsed
                  <<" ms; check the pasteboard before retrying.";break;
          case seethis::platform::AnnotatedCopyStatus::kPasteboardFailed:
            status<<"Pasteboard unavailable; marked image was not copied.";break;
          case seethis::platform::AnnotatedCopyStatus::kPasteboardChanged:
            status<<"Pasteboard changed during preparation; marked image was not copied.";break;
        }
        owner->_inspectorActionStatus=[NSString stringWithUTF8String:status.str().c_str()];
        [owner refreshInspector];
      });
}
- (void)inspectorDeleteSelected:(id)sender {
  (void)sender;
  const std::string selected=_inspectorLifecycle.selected_id;
  if(selected.empty() ||
     ![_inspectorView.selectedReferenceID isEqualToString:
         [NSString stringWithUTF8String:selected.c_str()]])return;
  if(!_references->LookupMetadata(selected)) {
    _inspectorActionStatus=@"Reference unavailable; nothing was deleted.";
    [self refreshInspector];return;
  }
  const auto result=_marks->DeleteHistoryJob(selected);
  _inspectorLifecycle=seethis::platform::InspectorAfterSuccessfulDelete(
      _inspectorLifecycle,result,_marks->inspector_jobs());
  [self prepareInspectorSelection:_inspectorLifecycle.selected_id];
  for(STOverlayPanel* panel in self.panels)[panel.contentView setNeedsDisplay:YES];
  _inspectorActionStatus=[NSString stringWithUTF8String:_marks->status().c_str()];
  [self refreshInspector];
  [self updatePointerPolicy:_lastPointer];
}
- (void)showAcknowledgementForID:(const std::string&)identifier {
  [_acknowledgementTimer invalidate];_acknowledgementTimer=nil;
  _acknowledgement= @"Link copied — paste into chat";
  _acknowledgementID=identifier;
  for(STOverlayPanel* panel in self.panels)[panel.contentView setNeedsDisplay:YES];
  __weak STOverlayCoordinator* weakSelf=self;
  _acknowledgementTimer=[NSTimer scheduledTimerWithTimeInterval:1.8 repeats:NO
    block:^(NSTimer*) {
      STOverlayCoordinator* owner=weakSelf;if(!owner)return;
      owner->_acknowledgement=nil;owner->_acknowledgementID.clear();
      for(STOverlayPanel* panel in owner.panels)[panel.contentView setNeedsDisplay:YES];
    }];
}
- (void)showInspector {
  _inspectorLifecycle=seethis::platform::ReduceInspectorLifecycle(
      _inspectorLifecycle,
      seethis::platform::InspectorLifecycleEvent::kExplicitShow);
  [self ensureInspectorOnVisibleScreen];
  [self refreshInspector];
  _inspectorPanel.ignoresMouseEvents=!seethis::core::InspectorConsumesPointer(true);
  [_inspectorPanel orderFrontRegardless];
}
- (void)showInspectorIfAllowed:(seethis::core::InspectorSelectionTrigger)trigger {
  if(seethis::core::InspectorAutoOpenAllowed(
         _inspectorLifecycle.user_hidden,trigger))
    [self showInspector];
  else if([self inspectorVisible])[self refreshInspector];
}
- (void)hideInspector {
  _inspectorLifecycle=seethis::platform::ReduceInspectorLifecycle(
      _inspectorLifecycle,
      seethis::platform::InspectorLifecycleEvent::kUserClose);
  _inspectorPanel.ignoresMouseEvents=YES;
  [_inspectorPanel orderOut:nil];
}
- (void)toggleInspector {
  _inspectorLifecycle=seethis::platform::ReduceInspectorLifecycle(
      _inspectorLifecycle,
      seethis::platform::InspectorLifecycleEvent::kExplicitToggle);
  if(_inspectorLifecycle.panel_visible) {
    // Connection has its own explicit action, independent of foreground app.
    // Merely showing Inspector never requests Automation consent.
    (void)seethis::platform::RefreshChromeConnection();
    [self showInspector];
  } else [self hideInspector];
}
- (void)ensureInspectorOnVisibleScreen {
  if(!_inspectorPanel||NSScreen.screens.count==0)return;
  NSRect frame=_inspectorPanel.frame;
  NSScreen* destination=nil;
  for(NSScreen* screen in NSScreen.screens)
    if(NSIntersectsRect(frame,screen.visibleFrame)){destination=screen;break;}
  if(!destination)destination=NSScreen.mainScreen ? NSScreen.mainScreen : NSScreen.screens.firstObject;
  NSRect visible=destination.visibleFrame;
  const CGFloat maxX=NSMaxX(visible)-NSWidth(frame);
  const CGFloat maxY=NSMaxY(visible)-NSHeight(frame);
  frame.origin.x=std::clamp(frame.origin.x,NSMinX(visible),maxX);
  frame.origin.y=std::clamp(frame.origin.y,NSMinY(visible),maxY);
  [_inspectorPanel setFrame:frame display:NO];
}
- (void)abortPending:(NSString*)reason {
  _deletion->Interrupt(_deletion->Snapshot().key_down);
  _inspectorCaptureSelectionPending=false;
  _marks->Abort(reason.UTF8String);
  for(STOverlayPanel* panel in self.panels){panel.ignoresMouseEvents=YES;[panel.contentView setNeedsDisplay:YES];}
}
- (void)rebuildDisplays {
  _deletion->Interrupt(_deletion->Snapshot().key_down);_hoveredID.clear();
  _lastPointer.reset();_pendingHoverPresentationDisplays.clear();
  if(_marks)_marks->Abort("display configuration changed");
  _displays=seethis::platform::SnapshotDisplays();
  [self ensureInspectorOnVisibleScreen];
  for(STOverlayPanel* panel in self.panels){[panel orderOut:nil];[panel close];}
  [self.panels removeAllObjects];
  for(NSScreen* screen in NSScreen.screens) {
    STOverlayPanel* panel=[[STOverlayPanel alloc] initWithContentRect:NSMakeRect(0,0,NSWidth(screen.frame),NSHeight(screen.frame))
      styleMask:NSWindowStyleMaskBorderless|NSWindowStyleMaskNonactivatingPanel backing:NSBackingStoreBuffered defer:NO screen:screen];
    panel.displayID=[screen.deviceDescription[@"NSScreenNumber"] unsignedIntValue];
    panel.level=NSStatusWindowLevel;panel.opaque=NO;panel.backgroundColor=NSColor.clearColor;panel.hasShadow=NO;
    panel.hidesOnDeactivate=NO;panel.ignoresMouseEvents=YES;panel.acceptsMouseMovedEvents=YES;
    panel.collectionBehavior=NSWindowCollectionBehaviorCanJoinAllSpaces|NSWindowCollectionBehaviorFullScreenAuxiliary|NSWindowCollectionBehaviorStationary;
    STOverlayView* view=[[STOverlayView alloc] initWithFrame:panel.contentView.bounds];
    view.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;view.coordinator=self;view.displayID=panel.displayID;
    panel.contentView=view;[panel orderFrontRegardless];[self.panels addObject:panel];
  }
}
#if defined(SEETHIS_WINDOW_OBSERVATION_API)
- (void)invalidateVisibilityPresentation {
  _hoveredID.clear();
  _pendingHoverPresentationDisplays.clear();
  for(STOverlayPanel* panel in self.panels)
    [panel.contentView setNeedsDisplay:YES];
  [self updatePointerPolicy:_lastPointer];
}
- (void)publishForegroundUnavailable {
  seethis::platform::PublishChromeObservation(0,false,
      seethis::core::PageAvailability::kUnavailable);
  _marks->SetWindowObservation({
      seethis::core::WindowAvailability::kUnavailable,
      ++_windowObservationGeneration,seethis::core::Now(),0,"",0,{}});
  const auto page_generation=_chromeLifecycle.Invalidate();
  _marks->SetPageObservation({
      seethis::core::PageAvailability::kUnavailable,page_generation,
      seethis::core::Now(),std::nullopt});
  if(_marks->Pending())
    [self abortPending:@"foreground application changed"];
  _captureContext.reset();
  [self invalidateVisibilityPresentation];
  if([self inspectorVisible])[self refreshInspector];
}
- (void)refreshForegroundWindow {
  const auto connection=seethis::platform::RefreshChromeConnection();
  if(connection.authorization_generation!=_chromePermissionGeneration) {
    _chromePermissionGeneration=connection.authorization_generation;
    const auto page_generation=_chromeLifecycle.Invalidate();
    _marks->SetPageObservation({seethis::core::PageAvailability::kUnavailable,
        page_generation,seethis::core::Now(),std::nullopt});
    [self invalidateVisibilityPresentation];
  }
  const auto previous=_marks->window_observation();
  auto current=seethis::platform::ObserveForegroundWindow(
      ++_windowObservationGeneration);
  const bool changed=!SameWindowIdentity(previous,current);
  _marks->SetWindowObservation(current);

  if(_marks->Pending() && _captureContext &&
     !seethis::core::WindowObservationMatchesContext(*_captureContext,current))
    [self abortPending:@"foreground window changed"];

  if(changed) {
    const auto page_generation=_chromeLifecycle.Invalidate();
    _marks->SetPageObservation({
        seethis::core::PageAvailability::kUnavailable,page_generation,
        seethis::core::Now(),std::nullopt});
    [self invalidateVisibilityPresentation];
    if([self inspectorVisible])[self refreshInspector];
  }

  const bool chrome=current.availability==
          seethis::core::WindowAvailability::kReady &&
      current.bundle_id=="com.google.Chrome";
  seethis::platform::PublishChromeObservation(current.pid,
      chrome && _marks->NeedsChromePageObservation(),
      _marks->page_observation().availability);
  if(chrome && _marks->NeedsChromePageObservation()) {
    [self refreshChromePage];
  } else if(_chromeLifecycle.pending() ||
            _marks->page_observation().availability!=
                seethis::core::PageAvailability::kUnavailable) {
    const auto page_generation=_chromeLifecycle.Invalidate();
    _marks->SetPageObservation({
        seethis::core::PageAvailability::kUnavailable,page_generation,
        seethis::core::Now(),std::nullopt});
    [self invalidateVisibilityPresentation];
  }
  if(!_marks->Pending())_captureContext.reset();
}
#endif
- (void)refreshChromePage {
#if defined(SEETHIS_WINDOW_OBSERVATION_API)
  if(_chromeLifecycle.pending() || !_marks->NeedsChromePageObservation())return;
  const auto foreground=_marks->window_observation();
  if(foreground.availability!=seethis::core::WindowAvailability::kReady ||
     foreground.bundle_id!="com.google.Chrome")return;
  const auto request=_chromeLifecycle.BeginRequest();
  const auto capture_generation=_generation;
  const auto permission_generation=seethis::platform::ChromeConnectionStatus().authorization_generation;
  __weak STOverlayCoordinator* weakSelf=self;
  seethis::platform::AcquireChromePageAsync(
      [foreground] {
        return seethis::platform::AcquireCurrentChromePage(foreground);
      }, request.generation,
      [weakSelf,foreground,request,capture_generation,permission_generation](
          seethis::core::PageObservation observation) {
    STOverlayCoordinator* owner=weakSelf;
    if(!owner || !owner->_chromeLifecycle.Accepts(request))return;
    if(permission_generation!=seethis::platform::ChromeConnectionStatus().authorization_generation)return;
    const auto current=owner->_marks->window_observation();
    if(!SameWindowIdentity(foreground,current)) {
      os_log_with_type(OS_LOG_DEFAULT,OS_LOG_TYPE_INFO,
          "seethis chrome phase=callback-validation cause=window-changed generation=%{public}llu pid=%{public}lld window=%{public}llu",
          static_cast<unsigned long long>(request.generation),
          static_cast<long long>(foreground.pid),
          static_cast<unsigned long long>(foreground.window_id));
      return;
    }
    owner->_chromeLifecycle.Complete(request);
    owner->_marks->SetPageObservation(std::move(observation));
    const auto published=owner->_marks->page_observation();
    seethis::platform::PublishChromeObservation(foreground.pid,true,
        published.availability,true);
    if(published.availability==seethis::core::PageAvailability::kReady &&
       published.identity) {
      (void)owner->_marks->BindPageIdentity(capture_generation,
                                            *published.identity);
      if(capture_generation==owner->_generation)[owner selectAcceptedInspectorCapture];
    } else if(published.availability!=
                  seethis::core::PageAvailability::kReady &&
              owner->_marks->AwaitingPageIdentity(capture_generation)) {
      const char* category=published.availability==
          seethis::core::PageAvailability::kDenied?"denied":
          published.availability==seethis::core::PageAvailability::kAmbiguous?
              "ambiguous":
          published.availability==seethis::core::PageAvailability::kTimedOut?
              "timed-out":"unavailable";
      os_log_with_type(OS_LOG_DEFAULT,OS_LOG_TYPE_INFO,
          "seethis chrome phase=mark-binding cause=page-not-ready category=%{public}s generation=%{public}llu capture-generation=%{public}llu pid=%{public}lld window=%{public}llu",
          category,static_cast<unsigned long long>(request.generation),
          static_cast<unsigned long long>(capture_generation),
          static_cast<long long>(foreground.pid),
          static_cast<unsigned long long>(foreground.window_id));
      NSString* reason=published.availability==
          seethis::core::PageAvailability::kDenied
          ? @"Chrome Automation permission unavailable; choose Connect Chrome or enable SeeThis > Google Chrome in System Settings > Privacy & Security > Automation, then choose Retry Chrome"
          : published.availability==seethis::core::PageAvailability::kAmbiguous
          ? @"Chrome window identity ambiguous; retry with Chrome frontmost"
          : published.availability==seethis::core::PageAvailability::kTimedOut
          ? @"Chrome page identity timed out; retry with Chrome frontmost"
          : @"Chrome page identity unavailable; retry with Chrome frontmost";
      [owner abortPending:reason];
    }
    [owner invalidateVisibilityPresentation];
  });
#else
  if(!_chromeContext)return;
  const auto captured=*_chromeContext;
  const auto capture_generation=_generation;
  const auto provider_generation=++_chromeObservationGeneration;
  __weak STOverlayCoordinator* weakSelf=self;
  seethis::platform::AcquireChromePageAsync(
      [captured] { return seethis::platform::AcquireCurrentChromePage(captured); },
      provider_generation,
      [weakSelf,capture_generation](seethis::core::PageObservation observation) {
    STOverlayCoordinator* owner=weakSelf;if(!owner)return;
    owner->_marks->SetPageObservation(observation);
    if(observation.availability==seethis::core::PageAvailability::kReady&&
       observation.identity)
      owner->_marks->BindPageIdentity(capture_generation,*observation.identity);
    if(capture_generation==owner->_generation)[owner selectAcceptedInspectorCapture];
    for(STOverlayPanel* panel in owner.panels)[panel.contentView setNeedsDisplay:YES];
    [owner updatePointerPolicy:owner->_lastPointer];
  });
#endif
}
- (void)stopChromePageRefresh {
  seethis::platform::PublishChromeObservation(0,false,
      seethis::core::PageAvailability::kUnavailable);
  [_chromePageTimer invalidate];_chromePageTimer=nil;_chromeContext.reset();
#if defined(SEETHIS_WINDOW_OBSERVATION_API)
  const auto generation=_chromeLifecycle.Invalidate();
  _marks->SetPageObservation({
      seethis::core::PageAvailability::kUnavailable,generation,
      seethis::core::Now(),std::nullopt});
  [self invalidateVisibilityPresentation];
#else
  _chromeObservationGeneration=0;
#endif
}
- (void)refresh {
  using namespace seethis::core;
  const auto snapshot=self.controller->Snapshot();
  if(snapshot.state==InteractionState::kDrawing && _previousState!=InteractionState::kDrawing) {
#if !defined(SEETHIS_WINDOW_OBSERVATION_API)
    [self stopChromePageRefresh];
#endif
    // No drawing or mouse capture is allowed before this synchronous snapshot.
    for(STOverlayPanel* panel in self.panels)panel.ignoresMouseEvents=YES;
    NSString* uuid=[NSUUID.UUID.UUIDString.lowercaseString stringByReplacingOccurrencesOfString:@"-" withString:@""];
    _activeID=uuid.UTF8String;
    _inspectorCaptureSucceeded=false;
    _inspectorCaptureSelectionPending=true;
    _generation=_marks->Begin(uuid.UTF8String,_settings->Get().crop_margin_points,
                              snapshot.key_down_monotonic_ms);
    for (const auto& display : _displays) {
      if (display.id == snapshot.session_origin.display_id) {
        self.controller->SetSelectedDisplayGeometry(
            {display.id, {display.logical.x, display.logical.y},
             {display.logical.width, display.logical.height}, display.scale});
        break;
      }
    }
    std::vector<std::uint64_t> excluded;for(STOverlayPanel* panel in self.panels)excluded.push_back(panel.windowNumber);
    std::string error;auto context=seethis::platform::SnapshotContext(snapshot.session_origin,excluded,error);
    if(!context)_marks->Abort(error);
    else {
      _originalPID=context->pid;
      [self activateInspectorChromeContext:context->bundle_id=="com.google.Chrome"];
      if(_marks->BindContext(_generation,*context)) {
        const auto generation=_generation;__weak STOverlayCoordinator* weakSelf=self;
#if defined(SEETHIS_WINDOW_OBSERVATION_API)
        _captureContext=*context;
        [self refreshForegroundWindow];
        if(context->bundle_id=="com.google.Chrome")[self refreshChromePage];
#else
        if(context->bundle_id=="com.google.Chrome") {
          _chromeContext=*context;
          [self refreshChromePage];
          __weak STOverlayCoordinator* timerSelf=self;
          _chromePageTimer=[NSTimer scheduledTimerWithTimeInterval:0.5 repeats:YES
              block:^(NSTimer*) { STOverlayCoordinator* owner=timerSelf;
                if(owner)[owner refreshChromePage]; }];
        }
#endif
#if defined(SEETHIS_WINDOW_OBSERVATION_API)
        if(_marks->Pending())
#endif
        seethis::platform::CaptureDisplay(*context,snapshot.session_origin.display_id,[weakSelf,generation](CaptureResult result) {
          STOverlayCoordinator* owner=weakSelf;if(!owner)return;
          const char* capture_result=!result.error.empty()?"failed-capture":
              !seethis::core::ValidPixels(result.pixels)?"invalid-capture":
              "capture-received";
          os_log_with_type(OS_LOG_DEFAULT,OS_LOG_TYPE_INFO,
              "seethis capture stage=capture-callback result=%{public}s",
              capture_result);
          owner->_marks->Expire(Now());
          [owner completeInspectorCapture:std::move(result) generation:generation];
          owner->_inspectorLifecycle=seethis::platform::ReduceInspectorLifecycle(
              owner->_inspectorLifecycle,
              seethis::platform::InspectorLifecycleEvent::kBackgroundCompletion);
          [owner refresh];
          [owner updatePointerPolicy:owner->_lastPointer];
        });
      }
    }
  } else if(snapshot.state==InteractionState::kFinished && _previousState==InteractionState::kDrawing) {
    // Release is immediate even when persistence must await this generation's image.
    for(STOverlayPanel* panel in self.panels)panel.ignoresMouseEvents=YES;
    _marks->Release(_generation,snapshot.subpaths,Now(),
                    snapshot.key_up_monotonic_ms);
    const bool invalid_path=_marks->status()=="Reference failed: invalid";
    const char* release_result=invalid_path?"invalid-path":
        _marks->phase()==ReferencePhase::kAborted?"rejected":"accepted";
    os_log_with_type(OS_LOG_DEFAULT,OS_LOG_TYPE_INFO,
        "seethis capture stage=reference-release completion=%{public}s "
        "result=%{public}s regions=%{public}llu",
        CompletionReasonName(snapshot.completion_reason),
        release_result,
        static_cast<unsigned long long>(snapshot.regions.size()));
    [self selectAcceptedInspectorCapture];
  } else if(snapshot.state==InteractionState::kIdle && _previousState==InteractionState::kDrawing) {
#if !defined(SEETHIS_WINDOW_OBSERVATION_API)
    [self stopChromePageRefresh];
#endif
    const auto reason=snapshot.cancel_reason;
    _inspectorCaptureSelectionPending=false;
    _marks->Abort(CancelReasonDescription(reason));
#if defined(SEETHIS_WINDOW_OBSERVATION_API)
    _captureContext.reset();
#endif
    os_log_with_type(OS_LOG_DEFAULT,OS_LOG_TYPE_INFO,
        "seethis capture stage=reference-cancel result=%{public}s",
        CancelReasonName(reason));
  }
  _previousState=snapshot.state;
  for(STOverlayPanel* panel in self.panels)[panel.contentView setNeedsDisplay:YES];
  if([self inspectorVisible])[self refreshInspector];
}
- (void)updatePointerPolicy:(const std::optional<seethis::core::DisplayPoint>&)pointer {
  using namespace seethis::core;
  _lastPointer=pointer;
  const auto before=_marks->phase();_marks->Expire(Now());
  if(_marks->Pending() && _originalPID!=NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier)_marks->Abort("foreground application changed");
  const bool drawing=self.controller->Snapshot().captures_pointer() && _marks->AllowsDrawing();
  const bool pending=_marks->Pending();
  const auto hit=!pending&&pointer?
      _marks->HitIdentity(*pointer,_settings->Get().mark_hit_radius_points,
                          _displays):std::optional<std::string>{};
  const auto hit_metadata=hit?_references->LookupMetadata(*hit):
      std::optional<seethis::core::ReferenceJobSnapshot>{};
  const std::string next_hover=hit?*hit:"";
  const bool hover_changed=next_hover!=_hoveredID;
  const std::string previous_hover=_hoveredID;
  _hoveredID=next_hover;
  const auto deletion=_deletion->Snapshot();
  bool pointer_panel_accepts=false;
  for(STOverlayPanel* panel in self.panels) {
    const bool hit_on_panel=hit.has_value()&&
        pointer->display_id==panel.displayID&&
        hit_metadata&&seethis::platform::ReferenceJobAcceptsPointer(
            hit_metadata->state);
    panel.ignoresMouseEvents=!seethis::platform::OverlayAcceptsPointer(
        drawing,hit_on_panel,deletion);
    if(pointer&&pointer->display_id==panel.displayID)
      pointer_panel_accepts=!panel.ignoresMouseEvents;
    if(before!=_marks->phase())
      [panel.contentView setNeedsDisplay:YES];
  }
  if(hover_changed) {
    const auto window=_marks->window_observation();
    const auto page=_marks->page_observation();
    const auto observed_at=seethis::core::Now();
    std::cerr<<"seethis: physical-hover stage=identity generation="
      <<(_hoverGeneration+1)<<" window_state="<<static_cast<int>(window.availability)
      <<" window_generation="<<window.generation
      <<" window_age_us="<<(observed_at.monotonic_us-window.observed.monotonic_us)
      <<" page_state="<<static_cast<int>(page.availability)
      <<" page_generation="<<page.generation
      <<" page_age_us="<<(observed_at.monotonic_us-page.observed.monotonic_us)
      <<" visible_marks="<<_marks->marks().size()
      <<" pointer_present="<<pointer.has_value()
      <<" pointer_panel_accepts="<<pointer_panel_accepts<<'\n';
    ++_hoverGeneration;_pendingHoverPresentationDisplays.clear();
    const auto invalidate=[&](const std::string& identity,
                              const auto& items) {
      for(const auto& item:items) {
        const auto& reference=item.value;
        if(reference.id!=identity)continue;
        for(STOverlayPanel* panel in self.panels) {
          const NSRect bounds=RegionBounds(seethis::core::EffectiveRegions(reference),panel.displayID);
          if(!NSIsEmptyRect(bounds)) {
            [panel.contentView setNeedsDisplayInRect:bounds];
            _pendingHoverPresentationDisplays.insert(panel.displayID);
            std::cerr<<"seethis: physical-hover stage=dirty generation="
              <<_hoverGeneration<<" id="<<identity<<" display="
              <<panel.displayID<<" x="<<bounds.origin.x<<" y="
              <<bounds.origin.y<<" width="<<bounds.size.width<<" height="
              <<bounds.size.height<<'\n';
          }
        }
      }
    };
    invalidate(previous_hover,_marks->marks());
    invalidate(next_hover,_marks->marks());
    const auto invalidate_jobs=[&](const std::string& identity) {
      for(const auto& job:_marks->jobs())if(job.id==identity)
        for(STOverlayPanel* panel in self.panels) {
          std::vector<seethis::core::ReferenceRegion> regions=job.regions;
          if(regions.empty()&&!job.path.empty())regions.push_back({job.path,{}, {}});
          const NSRect bounds=RegionBounds(regions,panel.displayID);
          if(!NSIsEmptyRect(bounds)) {
            [panel.contentView setNeedsDisplayInRect:bounds];
            _pendingHoverPresentationDisplays.insert(panel.displayID);
            std::cerr<<"seethis: physical-hover stage=dirty generation="
              <<_hoverGeneration<<" id="<<identity<<" display="
              <<panel.displayID<<" x="<<bounds.origin.x<<" y="
              <<bounds.origin.y<<" width="<<bounds.size.width<<" height="
              <<bounds.size.height<<'\n';
          }
        }
    };
    invalidate_jobs(previous_hover);invalidate_jobs(next_hover);
    std::cerr<<"seethis: physical-hover stage=transition generation="
      <<_hoverGeneration<<" previous="
      <<(previous_hover.empty()?"none":previous_hover)<<" current="
      <<(next_hover.empty()?"none":next_hover)<<" pointer_display="
      <<(pointer?pointer->display_id:0)<<" dirty_displays="
      <<_pendingHoverPresentationDisplays.size()<<'\n';
    const auto displays=_pendingHoverPresentationDisplays;
    for(STOverlayPanel* panel in self.panels)
      if(displays.contains(panel.displayID))[panel.contentView displayIfNeeded];
  }
}
- (void)paintDisplay:(seethis::core::DisplayId)display {
  for(const auto& stored:_marks->marks()) {
    const auto& r=stored.value;if(!seethis::core::DisplayTopologyMatches(r.context.displays,_displays))continue;
    StrokeRegions(seethis::core::EffectiveRegions(r),display,
           MarkColor(seethis::platform::InspectorMarkIsSelected(
               r.id,_inspectorLifecycle.selected_id)),
           seethis::platform::MarkStrokeWidth(r.id==_hoveredID));
    if(_acknowledgement&&r.id==_acknowledgementID) {
      bool labeled = false;
      for(const auto& region:seethis::core::EffectiveRegions(r))
        seethis::core::ForEachSubpath(region, [&](const auto& path) {
          for(const auto& point:path) if(!labeled && point.display_id==display) {
            Label(_acknowledgement,NSMakePoint(point.position.x+8,point.position.y+8));
            labeled = true;
          }
        });
    }
  }
  for(const auto& job:_marks->jobs()) {
    if(!seethis::core::DisplayTopologyMatches(job.context.displays,_displays))continue;
    NSColor* color=seethis::platform::InspectorMarkIsSelected(
      job.id,_inspectorLifecycle.selected_id)?
      MarkColor(true):job.state==seethis::core::ReferenceJobState::kPending?
      [NSColor colorWithSRGBRed:0.1 green:0.55 blue:1 alpha:0.92]:
      job.state==seethis::core::ReferenceJobState::kFailed?
      [NSColor colorWithSRGBRed:0.9 green:0.15 blue:0.15 alpha:0.92]:
      [NSColor colorWithSRGBRed:1 green:0.48 blue:0.08 alpha:0.92];
    std::vector<seethis::core::ReferenceRegion> regions=job.regions;
    if(regions.empty()&&!job.path.empty())regions.push_back({job.path,{}, {}});
    StrokeRegions(regions,display,color,
           seethis::platform::MarkStrokeWidth(job.id==_hoveredID));
    if(_acknowledgement&&job.id==_acknowledgementID) {
      bool labeled = false;
      for(const auto& region:regions)
        seethis::core::ForEachSubpath(region, [&](const auto& path) {
          for(const auto& point:path) if(!labeled && point.display_id==display) {
            Label(_acknowledgement,NSMakePoint(point.position.x+8,point.position.y+8));
            labeled = true;
          }
        });
    }
  }
  if(_marks->AllowsDrawing())for(const auto& region:self.controller->Snapshot().subpaths)
    for(const auto& path:region) Stroke(path,display,[NSColor colorWithSRGBRed:0.1 green:0.55 blue:1 alpha:0.95]);
  if(_pendingHoverPresentationDisplays.erase(display)>0&&
     _pendingHoverPresentationDisplays.empty()) {
    _presentedHoverGeneration=_hoverGeneration;
    std::cerr<<"seethis: physical-hover stage=presented generation="
      <<_presentedHoverGeneration<<" hovered="
      <<(_hoveredID.empty()?"none":_hoveredID)<<" visible_line_width="
      <<seethis::platform::MarkStrokeWidth(!_hoveredID.empty())<<'\n';
  }
}
- (BOOL)click:(seethis::core::DisplayPoint)point {
  if(_marks->Pending())return NO;
  const auto settings=_settings->Get();
  const auto hit=_marks->HitIdentity(point,settings.mark_hit_radius_points,
                                     _displays);
  if(!hit||*hit!=_hoveredID)return NO;
  const auto flags=CGEventSourceFlagsState(
      kCGEventSourceStateCombinedSessionState)&
      (kCGEventFlagMaskCommand|kCGEventFlagMaskControl|
       kCGEventFlagMaskAlternate|kCGEventFlagMaskShift);
  CGEventFlags required=0;
  if(settings.delete_shortcut_modifiers&seethis::core::kShortcutCommand)
    required|=kCGEventFlagMaskCommand;
  if(settings.delete_shortcut_modifiers&seethis::core::kShortcutControl)
    required|=kCGEventFlagMaskControl;
  if(settings.delete_shortcut_modifiers&seethis::core::kShortcutOption)
    required|=kCGEventFlagMaskAlternate;
  if(settings.delete_shortcut_modifiers&seethis::core::kShortcutShift)
    required|=kCGEventFlagMaskShift;
  const bool key_down=CGEventSourceKeyState(
      kCGEventSourceStateCombinedSessionState,
      static_cast<CGKeyCode>(settings.delete_shortcut_key_code));
  const bool claimed=_deletion->ConsumeDeleteClick(key_down,flags==required);
  std::optional<seethis::core::DeleteResult> delete_result;
  bool copied=false;
  if(claimed) {
    delete_result=_marks->DeleteJob(*hit);
    _inspectorLifecycle=seethis::platform::InspectorAfterSuccessfulDelete(
        _inspectorLifecycle,*delete_result,_marks->inspector_jobs());
    [self prepareInspectorSelection:_inspectorLifecycle.selected_id];
    _inspectorActionStatus=[NSString stringWithUTF8String:_marks->status().c_str()];
    [self refreshInspector];
  } else if(!_deletion->Snapshot().blocks_mark_copy()) {
    _inspectorActionStatus=nil;
    _inspectorLifecycle=seethis::platform::ReduceInspectorLifecycle(
        _inspectorLifecycle,
        seethis::platform::InspectorLifecycleEvent::kReferenceSelection,
        *hit);
    [self prepareInspectorSelection:_inspectorLifecycle.selected_id];
    copied=[self recopyInspectorSelection:*hit];
    [self showInspectorIfAllowed:seethis::core::InspectorSelectionTrigger::kMarkClick];
  }
  std::cerr<<"seethis: physical-delete stage=native-click id="<<*hit
    <<" physical_key_down="<<key_down<<" exact_modifiers="<<(flags==required)
    <<" claimed="<<claimed<<" disposition="
    <<(delete_result?DeleteResultName(*delete_result):(copied?"copied":"passed"))
    <<'\n';
  for(STOverlayPanel* panel in self.panels)[panel.contentView setNeedsDisplay:YES];
  [self updatePointerPolicy:point];
  return claimed;
}
- (BOOL)beginRegion:(seethis::core::DisplayPoint)point {
  if(!self.controller->Snapshot().captures_pointer())return NO;
  const bool began=self.controller->PointerDown(point);
  if(began){[self refresh];[self updatePointerPolicy:point];}
  return YES;
}
- (void)moveRegion:(seethis::core::DisplayPoint)point {
  if(self.controller->PointerMoved(point))[self refresh];
  [self updatePointerPolicy:point];
}
- (void)endRegion:(seethis::core::DisplayPoint)point {
  if(self.controller->PointerUp(point))[self refresh];
  [self updatePointerPolicy:point];
}
- (std::optional<seethis::core::DisplayPoint>)displayPointAtGlobalLogical:(seethis::core::Point2D)global {
  const auto snapshot=self.controller->Snapshot();
  if (snapshot.state == seethis::core::InteractionState::kDrawing) {
    for (const auto& display : _displays) {
      if (display.id == snapshot.session_origin.display_id) {
        return seethis::core::DisplayPoint{
            display.id, seethis::core::CoordinateUnit::kLogicalPoints,
            {global.x - display.logical.x, global.y - display.logical.y},
            display.scale};
      }
    }
  }
  for(const auto& d:_displays) {
    seethis::core::DisplayGeometry geometry{d.id,{d.logical.x,d.logical.y},{d.logical.width,d.logical.height},d.scale};
    if(auto point=geometry.FromGlobalLogical(global))return point;
  }return {};
}
@end

class MacOverlayAdapter final:public seethis::platform::OverlayAdapter {
 public:
  MacOverlayAdapter(seethis::core::InteractionController* controller,
                    seethis::core::ReferenceStore* references,
                    seethis::core::SettingsStore* settings,
                    seethis::service::ReferenceServer* server,
                    seethis::core::DeleteGestureController* deletion)
      :coordinator_([[STOverlayCoordinator alloc] initWithController:controller
          references:references settings:settings server:server
          deletion:deletion]){}
  void Refresh() override {[coordinator_ refresh];}
  void RebuildDisplays() override {[coordinator_ rebuildDisplays];}
  void UpdatePointerPolicy(const std::optional<seethis::core::DisplayPoint>& pointer) override {[coordinator_ updatePointerPolicy:pointer];}
  std::optional<seethis::core::DisplayPoint> DisplayPointAtGlobalLogical(seethis::core::Point2D point) const override {
    return [coordinator_ displayPointAtGlobalLogical:point];
  }
  void ToggleInspector() override {[coordinator_ toggleInspector];}
  bool InspectorVisible() const override {return [coordinator_ inspectorVisible];}
 private:
  __strong STOverlayCoordinator* coordinator_;
};
namespace seethis::platform {
std::unique_ptr<OverlayAdapter> MakeOverlayAdapter(core::InteractionController* controller,
    core::ReferenceStore* references,core::SettingsStore* settings,
    service::ReferenceServer* server,core::DeleteGestureController* deletion) {
  return std::make_unique<MacOverlayAdapter>(controller,references,settings,server,
                                             deletion);
}
}
#endif  // !SEETHIS_INSPECTOR_FEEDBACK_TESTING
