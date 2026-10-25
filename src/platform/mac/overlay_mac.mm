#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <os/log.h>
#include <algorithm>
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
@property(nonatomic,copy) NSArray<NSString*>* referenceFullRows;
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
  self.popup.toolTip=nil;self.popup.accessibilityValue=nil;
  for(NSUInteger i=0;i<self.referenceIDs.count;++i) {
    NSString* identifier=self.referenceIDs[i];
    // NSPopUpButton's title convenience method coalesces duplicate labels.
    // Distinct references must retain distinct native items and stable IDs.
    NSMenuItem* item=[[NSMenuItem alloc] initWithTitle:self.referenceRows[i]
        action:@selector(chooseReference:) keyEquivalent:@""];
    [self.popup.menu addItem:item];
    item.representedObject=identifier;item.target=self;
    item.action=@selector(chooseReference:);
    NSString* full=i<self.referenceFullRows.count?self.referenceFullRows[i]:self.referenceRows[i];
    item.toolTip=full;item.accessibilityLabel=full;
    if([identifier isEqualToString:self.selectedReferenceID]) {
      [self.popup selectItem:item];
      self.popup.toolTip=full;self.popup.accessibilityValue=full;
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
@property(nonatomic,strong) NSButton* markVisibilityButton;
@property(nonatomic,strong) NSButton* deleteAllButton;
@property(nonatomic,strong) NSButton* appendModeButton;
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
  // These sole action instances live outside the scrolling document. The
  // container owns their geometry, including longer persistence retry titles.
  const BOOL hasActions=self.markVisibilityButton && self.deleteAllButton;
  const CGFloat actionHeight=hasActions?48:0;
  if(hasActions) {
    const CGFloat deleteWidth=ceil(self.deleteAllButton.fittingSize.width)+12;
    const CGFloat visibilityWidth=ceil(self.markVisibilityButton.fittingSize.width)+12;
    const CGFloat trailing=width-16;
    self.deleteAllButton.frame=NSMakeRect(trailing-deleteWidth,header+7,deleteWidth,34);
    self.markVisibilityButton.frame=NSMakeRect(trailing-deleteWidth-8-visibilityWidth,
        header+7,visibilityWidth,34);
  }
  const CGFloat modeHeight=self.appendModeButton?28:0;
  self.appendModeButton.frame=NSMakeRect(16,header+actionHeight,MAX(1,width-32),24);
  const CGFloat bodyTop=header+actionHeight+modeHeight;
  self.referenceScrollView.frame=NSMakeRect(0,bodyTop,width,MAX(0,NSHeight(self.bounds)-bodyTop));
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
  self.buttons[0].enabled=!state.consent_pending;
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
        state.consent_pending?@"request in progress":
            text(seethis::platform::ChromeConsentName(state.permission.consent))];
    self.guidanceLabel.stringValue=state.consent_pending
        ?@"Respond to the macOS Automation dialog. Connect will be available after it returns."
        :state.permission.consent==seethis::platform::ChromeConsent::kDenied
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

static std::vector<std::uint64_t> STCaptureExcludedWindowIDs(
    NSArray<NSWindow*>* drawingPanels, NSWindow* inspector,NSWindow* note=nil) {
  std::vector<std::uint64_t> excluded;
  const auto append = [&excluded](NSWindow* window) {
    if(!window)return;
    const NSInteger number=window.windowNumber;
    if(number<=0)return;
    const auto id=static_cast<std::uint64_t>(number);
    if(std::find(excluded.begin(),excluded.end(),id)==excluded.end())
      excluded.push_back(id);
  };
  for(NSWindow* panel in drawingPanels)append(panel);
  // Include the current Inspector even while hidden: it may become visible
  // before asynchronous capture completes. Never cache a window number.
  append(inspector);append(note);
  return excluded;
}

#if !defined(SEETHIS_INSPECTOR_FEEDBACK_TESTING)
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <unordered_map>
#include "platform/platform.h"
#include "core/reference.h"
#include "service/reference_server.h"

// The Inspector and its isolated native test share this action state. A dialog
// reviews one ordered set; later jobs never enter that set during confirmation.
static NSInteger STOwnPresentationPanelLevel() {return NSStatusWindowLevel+1;}

class STDeleteAllReferencesAction {
 public:
  struct Review {
    std::uint64_t generation;
    std::vector<std::string> ids;
  };
  struct Result {
    std::size_t deleted = 0;
    std::size_t already_deleted = 0;
    std::size_t unavailable = 0;
    std::size_t cleanup_failed = 0;
  };
  bool hasPendingReview() const {return !reviewed_ids_.empty();}
  bool dialogOpen() const {return dialog_open_;}
  std::optional<Review> Begin(const std::vector<seethis::core::ReferenceJobSnapshot>& jobs) {
    if(dialog_open_)return {};
    if(reviewed_ids_.empty()) {
      for(const auto& job:jobs)
        if(job.state!=seethis::core::ReferenceJobState::kDeleted)
          reviewed_ids_.push_back(job.id);
    }
    if(reviewed_ids_.empty())return {};
    dialog_open_=true;
    return Review{++generation_,reviewed_ids_};
  }
  bool Cancel(std::uint64_t generation) {
    if(!dialog_open_ || generation!=generation_)return false;
    dialog_open_=false;
    if(!retry_cleanup_)reviewed_ids_.clear();
    ++generation_;
    return true;
  }
  std::optional<Result> Confirm(std::uint64_t generation,
      const std::function<seethis::core::DeleteResult(std::string_view)>& remove) {
    if(!dialog_open_ || generation!=generation_)return {};
    dialog_open_=false;
    ++generation_;  // A late sheet callback cannot act on the next review.
    Result result;
    for(const auto& id:reviewed_ids_) {
      seethis::core::DeleteResult outcome=seethis::core::DeleteResult::kCleanupFailed;
      try {outcome=remove(id);} catch(...) {}
      switch(outcome) {
        case seethis::core::DeleteResult::kDeleted:++result.deleted;break;
        case seethis::core::DeleteResult::kAlreadyDeleted:++result.already_deleted;break;
        case seethis::core::DeleteResult::kNotFound:++result.unavailable;break;
        case seethis::core::DeleteResult::kCleanupFailed:++result.cleanup_failed;break;
      }
    }
    retry_cleanup_=result.cleanup_failed>0;
    if(!retry_cleanup_)reviewed_ids_.clear();
    return result;
  }
 private:
  std::vector<std::string> reviewed_ids_;
  std::uint64_t generation_ = 0;
  bool dialog_open_ = false;
  bool retry_cleanup_ = false;
};

// Kept outside the app-only section so the native editor can be exercised with
// a fake store. The production callbacks below are the only persistence path.
enum class STNoteReadStatus { kReady, kPending, kUnavailable };
struct STNoteReadResult {
  STNoteReadStatus status = STNoteReadStatus::kUnavailable;
  std::string note;
  std::uint64_t revision = 0;
};
enum class STNoteSaveStatus {
  kUpdated, kConflict, kUnavailable, kFailed, kCommitUncertain
};
struct STNoteSaveResult {
  STNoteSaveStatus status = STNoteSaveStatus::kFailed;
  std::uint64_t revision = 0;
};
enum class STNoteCopyStatus { kCopied, kUnavailable, kFailed };
using STNoteCopy = std::function<STNoteCopyStatus(std::string_view)>;
using STNoteOwnerValid = std::function<bool(std::string_view,std::string_view)>;
using STNoteRead = std::function<STNoteReadResult(std::string_view,std::string_view)>;
using STNoteSave = std::function<STNoteSaveResult(
    std::string_view,std::string_view,std::string_view,std::uint64_t)>;

@interface STMarkNotePanel : NSPanel
@property(nonatomic,weak) NSTextView* noteTextView;
@end
@implementation STMarkNotePanel
- (BOOL)canBecomeKeyWindow {return YES;}
- (BOOL)canBecomeMainWindow {return NO;}
// Escape is ordinary cancellation, never the explicit titlebar completion.
- (void)cancelOperation:(id)sender {(void)sender;}
- (BOOL)performKeyEquivalent:(NSEvent*)event {
  NSTextView* text=self.noteTextView;
  // The accessory app has no Edit main menu. Forward only this focused note's
  // standard edit chords; NSTextView retains editing, IME and undo bookkeeping.
  const auto modifiers=event.modifierFlags & NSEventModifierFlagDeviceIndependentFlagsMask;
  if(event.type==NSEventTypeKeyDown && self.isKeyWindow && self.firstResponder==text &&
     text && !text.hasMarkedText && modifiers==NSEventModifierFlagCommand) {
    NSString* key=event.charactersIgnoringModifiers.lowercaseString;
    SEL action=nullptr;
    if([key isEqualToString:@"c"])action=@selector(copy:);
    else if([key isEqualToString:@"x"])action=@selector(cut:);
    else if([key isEqualToString:@"v"])action=@selector(paste:);
    else if([key isEqualToString:@"a"])action=@selector(selectAll:);
    else if([key isEqualToString:@"z"]) {
      if(text.undoManager.canUndo)[text.undoManager undo];
      return YES;
    }
    if(action && [text tryToPerform:action with:self])return YES;
  }
  return [super performKeyEquivalent:event];
}
@end

@interface STMarkNoteEditor : NSObject <NSTextViewDelegate,NSWindowDelegate> {
 @private
  STNoteRead _read;
  STNoteSave _save;
  STNoteCopy _copy;
  STNoteOwnerValid _ownerValid;
  bool _completionRequested;
  bool _saving;
  std::uint64_t _draftGeneration;
  std::string _referenceID;
  std::string _markID;
  std::string _draft;
  std::uint64_t _revision;
  bool _dirty;
  bool _presentationOpen;
  std::uint64_t _presentationGeneration;
  std::unordered_map<std::string,std::string> _unsavedDrafts;
}
@property(nonatomic,strong,readonly) STMarkNotePanel* panel;
@property(nonatomic,strong,readonly) NSTextView* textView;
@property(nonatomic,strong,readonly) NSTextField* statusLabel;
@property(nonatomic,strong,readonly) NSButton* retryButton;
@property(nonatomic,strong) NSTimer* saveTimer;
@property(nonatomic,strong,readonly) id localMouseMonitor;
@property(nonatomic,strong,readonly) id globalMouseMonitor;
- (instancetype)initWithRead:(STNoteRead)read save:(STNoteSave)save;
- (instancetype)initWithRead:(STNoteRead)read save:(STNoteSave)save copy:(STNoteCopy)copy;
- (void)setOwnerValidation:(STNoteOwnerValid)validation;
- (void)setCopyCallback:(STNoteCopy)copy;
- (void)complete:(id)sender;
- (void)discardOwner;
- (BOOL)saveDraft:(BOOL)force;
- (BOOL)flushForAppend:(const std::string&)reference;
- (void)toggleReference:(const std::string&)referenceID
                  mark:(const std::string&)markID
                number:(std::uint32_t)number
            screenPoint:(NSPoint)screenPoint;
- (void)openReference:(const std::string&)referenceID mark:(const std::string&)markID
              number:(std::uint32_t)number screenPoint:(NSPoint)screenPoint;
- (std::string)referenceID;
- (std::string)markID;
- (std::uint64_t)presentationGeneration;
- (BOOL)presentationOpen;
- (void)autosaveTimer:(NSTimer*)timer generation:(std::uint64_t)generation;
- (NSEvent*)observeLocalMouseDown:(NSEvent*)event generation:(std::uint64_t)generation;
- (void)dismissOutsideForGeneration:(std::uint64_t)generation;
- (void)saveNow;
- (void)hide;
- (void)hideIfEditingReference:(const std::string&)referenceID
                         mark:(const std::string&)markID;
@end
@implementation STMarkNoteEditor
- (instancetype)initWithRead:(STNoteRead)read save:(STNoteSave)save {
  return [self initWithRead:std::move(read) save:std::move(save) copy:STNoteCopy{}];
}
- (instancetype)initWithRead:(STNoteRead)read save:(STNoteSave)save copy:(STNoteCopy)copy {
  self=[super init];if(!self)return nil;
  _read=std::move(read);_save=std::move(save);_copy=std::move(copy);_revision=0;_dirty=false;
  _panel=[[STMarkNotePanel alloc]
      initWithContentRect:NSMakeRect(0,0,340,224)
      styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|
          NSWindowStyleMaskNonactivatingPanel
      backing:NSBackingStoreBuffered defer:NO];
  _panel.level=STOwnPresentationPanelLevel();
  _panel.floatingPanel=YES;
  _panel.hidesOnDeactivate=NO;
  _panel.releasedWhenClosed=NO;
  _panel.delegate=self;
  NSButton* close=[_panel standardWindowButton:NSWindowCloseButton];
  close.target=self;close.action=@selector(complete:);
  NSView* content=_panel.contentView;
  NSScrollView* scroll=[[NSScrollView alloc]
      initWithFrame:NSMakeRect(12,48,316,164)];
  scroll.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;
  scroll.hasVerticalScroller=YES;
  scroll.borderType=NSBezelBorder;
  _textView=[[NSTextView alloc] initWithFrame:scroll.contentView.bounds];
  _textView.autoresizingMask=NSViewWidthSizable;
  _textView.minSize=NSMakeSize(0,0);
  _textView.maxSize=NSMakeSize(CGFLOAT_MAX,CGFLOAT_MAX);
  _textView.verticallyResizable=YES;
  _textView.horizontallyResizable=NO;
  _textView.textContainer.widthTracksTextView=YES;
  _textView.richText=NO;
  _textView.allowsUndo=YES;
  _panel.noteTextView=_textView;
  _textView.font=[NSFont systemFontOfSize:14];
  _textView.delegate=self;
  scroll.documentView=_textView;
  [content addSubview:scroll];
  _statusLabel=[NSTextField labelWithString:@""];
  _statusLabel.frame=NSMakeRect(12,12,236,26);
  _statusLabel.autoresizingMask=NSViewWidthSizable;
  _statusLabel.font=[NSFont systemFontOfSize:12];
  _statusLabel.lineBreakMode=NSLineBreakByTruncatingTail;
  [content addSubview:_statusLabel];
  _retryButton=[NSButton buttonWithTitle:@"Retry save" target:self
                                  action:@selector(retrySave:)];
  _retryButton.frame=NSMakeRect(252,8,76,32);
  _retryButton.autoresizingMask=NSViewMinXMargin;
  _retryButton.hidden=YES;
  [content addSubview:_retryButton];
  return self;
}
- (void)dealloc {
  [_saveTimer invalidate];
  if(_localMouseMonitor)[NSEvent removeMonitor:_localMouseMonitor];
  if(_globalMouseMonitor)[NSEvent removeMonitor:_globalMouseMonitor];
  _panel.delegate=nil;[_panel orderOut:nil];
}
- (void)stopMouseMonitoring {
  if(_localMouseMonitor)[NSEvent removeMonitor:_localMouseMonitor];
  if(_globalMouseMonitor)[NSEvent removeMonitor:_globalMouseMonitor];
  _localMouseMonitor=nil;_globalMouseMonitor=nil;
}
// Ordinary outside clicks are observation only. Never change owner, text,
// selection, key focus, or save/copy intent in response to the receiving app.
- (NSEvent*)observeLocalMouseDown:(NSEvent*)event generation:(std::uint64_t)generation {
  (void)generation;return event;
}
- (void)dismissOutsideForGeneration:(std::uint64_t)generation {(void)generation;}
- (BOOL)textView:(NSTextView*)textView doCommandBySelector:(SEL)selector {
  // The input client gets first chance to cancel composition. Unmarked Escape
  // reaching the delegate is consumed without a window-close action.
  return textView==_textView && selector==@selector(cancelOperation:) && !textView.hasMarkedText;
}
- (STNoteReadResult)readReference:(const std::string&)reference mark:(const std::string&)mark {
  try {return _read?_read(reference,mark):STNoteReadResult{};}catch(...) {return {};}
}
- (void)setOwnerValidation:(STNoteOwnerValid)validation {_ownerValid=std::move(validation);}
- (void)setCopyCallback:(STNoteCopy)copy {_copy=std::move(copy);}
- (BOOL)ownerValid {
  try {return !_ownerValid || _ownerValid(_referenceID,_markID);}catch(...) {return NO;}
}
- (void)captureLiveDraft {
  // Marked text can still be present when focus leaves without textDidChange.
  // Finish composition before reusing the shared text view for another owner.
  if(_textView.hasMarkedText)[_textView unmarkText];
  const std::string live=_textView.string.UTF8String?:"";
  if(live!=_draft) {
    ++_draftGeneration;_completionRequested=false;
    _draft=live;_dirty=true;_unsavedDrafts[[self draftKey]]=_draft;
  }
}
- (std::string)draftKey {
  return _referenceID+"\x1f"+_markID;
}
- (void)setStatus:(NSString*)status retry:(BOOL)retry {
  _statusLabel.stringValue=status;
  _retryButton.hidden=!retry;
}
- (void)openReference:(const std::string&)referenceID mark:(const std::string&)markID
              number:(std::uint32_t)number screenPoint:(NSPoint)screenPoint {
  // Explicit Inspector editing focuses an existing owner; it never finishes or
  // copies that note. Switching owners retains the existing save/conflict path.
  if(_presentationOpen && _referenceID==referenceID && _markID==markID) {
    [_panel makeKeyAndOrderFront:nil];[_panel makeFirstResponder:_textView];return;
  }
  [self toggleReference:referenceID mark:markID number:number screenPoint:screenPoint];
}
- (void)toggleReference:(const std::string&)referenceID
                  mark:(const std::string&)markID
                number:(std::uint32_t)number
            screenPoint:(NSPoint)screenPoint {
  if(_presentationOpen && _referenceID==referenceID && _markID==markID) {
    [self complete:nil];return;
  }
  if(_presentationOpen) {
    // A switch is save-only, including its failure/retry path.
    _completionRequested=false;_retryButton.title=@"Retry save";
    [self captureLiveDraft];
    const auto generation=_presentationGeneration;
    if(![self saveDraft:YES] || !_presentationOpen || generation!=_presentationGeneration)return;
  }
  _completionRequested=false;
  [_saveTimer invalidate];_saveTimer=nil;
  ++_presentationGeneration;
  _referenceID=referenceID;_markID=markID;
  const auto loaded=[self readReference:referenceID mark:markID];
  _revision=loaded.revision;
  const auto retained=_unsavedDrafts.find([self draftKey]);
  _dirty=retained!=_unsavedDrafts.end();
  _draft=_dirty?retained->second:loaded.note;
  _textView.delegate=nil;
  // The view is reused across reference/mark owners. Its prior undo records
  // must never insert another owner's bytes into this newly loaded note.
  [_textView breakUndoCoalescing];
  [_textView.undoManager removeAllActions];
  _textView.string=[NSString stringWithUTF8String:_draft.c_str()]?:@"";
  _textView.delegate=self;
  _panel.title=[NSString stringWithFormat:@"Note %u",number];
  if(_dirty)[self setStatus:@"Not saved. Your text is retained." retry:YES];
  else if(loaded.status==STNoteReadStatus::kPending)
    [self setStatus:@"Reference is preparing." retry:NO];
  else if(loaded.status==STNoteReadStatus::kUnavailable)
    [self setStatus:@"Notes are unavailable." retry:NO];
  else [self setStatus:@"Saved" retry:NO];
  NSScreen* screen=nil;
  for(NSScreen* candidate in NSScreen.screens)
    if(NSPointInRect(screenPoint,candidate.frame)) {screen=candidate;break;}
  if(!screen)screen=NSScreen.mainScreen;
  const NSRect visible=screen?screen.visibleFrame:NSMakeRect(0,0,1024,768);
  const CGFloat x=MIN(MAX(screenPoint.x+18,NSMinX(visible)),
                      NSMaxX(visible)-NSWidth(_panel.frame));
  const CGFloat y=MIN(MAX(screenPoint.y-NSHeight(_panel.frame)/2,NSMinY(visible)),
                      NSMaxY(visible)-NSHeight(_panel.frame));
  [_panel setFrameOrigin:NSMakePoint(x,y)];
  _presentationOpen=true;
  [_panel makeKeyAndOrderFront:nil];
  [_panel makeFirstResponder:_textView];
}
- (void)textDidChange:(NSNotification*)notification {
  if(notification.object!=_textView || !_presentationOpen)return;
  ++_draftGeneration;_completionRequested=false;
  _retryButton.title=@"Retry save";
  _draft=_textView.string.UTF8String?:"";
  _unsavedDrafts[[self draftKey]]=_draft;
  _dirty=true;
  [self setStatus:@"Pending save…" retry:NO];
  [_saveTimer invalidate];
  const auto generation=_presentationGeneration;
  __weak STMarkNoteEditor* weakSelf=self;
  _saveTimer=[NSTimer scheduledTimerWithTimeInterval:0.35 repeats:NO
      block:^(NSTimer* timer) {
        [weakSelf autosaveTimer:timer generation:generation];
      }];
}
- (void)autosaveTimer:(NSTimer*)timer generation:(std::uint64_t)generation {
  if(_presentationOpen && _saveTimer==timer &&
     _presentationGeneration==generation)[self saveNow];
}
- (BOOL)flushForAppend:(const std::string&)reference {
  if(_referenceID!=reference)return YES;
  if(_textView.hasMarkedText)return NO;
  [self captureLiveDraft];return [self saveDraft:NO];
}
- (void)saveNow {
  if(_textView.hasMarkedText)return;
  [self captureLiveDraft];[self saveDraft:NO];
}
- (BOOL)saveDraft:(BOOL)force {
  [_saveTimer invalidate];_saveTimer=nil;
  if(_saving || _referenceID.empty() || _markID.empty())return NO;
  if(!_dirty && !force)return YES;
  const auto generation=_presentationGeneration;
  const auto reference=_referenceID,mark=_markID,draft=_draft;
  const auto before=_revision,draftGeneration=_draftGeneration;
  // Even a clean completion must obtain a durable success result. A pending
  // read or matching bytes after an uncertain commit are not such a receipt.
  _dirty=true;_unsavedDrafts[[self draftKey]]=_draft;
  [self setStatus:@"Saving…" retry:NO];
  STNoteSaveResult outcome;
  _saving=true;
  try {if(_save)outcome=_save(reference,mark,draft,before);}catch(...) {}
  _saving=false;
  if(generation!=_presentationGeneration || reference!=_referenceID ||
     mark!=_markID || draft!=_draft || draftGeneration!=_draftGeneration)return NO;
  if(outcome.status==STNoteSaveStatus::kUpdated) {
    _revision=outcome.revision;_dirty=false;
    _unsavedDrafts.erase([self draftKey]);
    [self setStatus:@"Saved" retry:NO];return YES;
  }
  if(outcome.status==STNoteSaveStatus::kCommitUncertain ||
     outcome.status==STNoteSaveStatus::kConflict ||
     outcome.status==STNoteSaveStatus::kUnavailable) {
    const auto observed=[self readReference:reference mark:mark];
    if(generation!=_presentationGeneration || reference!=_referenceID ||
       mark!=_markID || draft!=_draft || draftGeneration!=_draftGeneration)return NO;
    if(observed.status==STNoteReadStatus::kReady &&
       (outcome.status==STNoteSaveStatus::kConflict ||
        (observed.revision>before && observed.note==draft)))_revision=observed.revision;
    if(outcome.status==STNoteSaveStatus::kCommitUncertain)
      [self setStatus:@"Save uncertain. Draft retained; retry." retry:YES];
    else if(outcome.status==STNoteSaveStatus::kConflict)
      [self setStatus:@"Changed elsewhere. Retry to replace." retry:YES];
    else if(observed.status==STNoteReadStatus::kPending)
      [self setStatus:@"Reference preparing. Draft retained." retry:YES];
    else [self setStatus:@"Reference unavailable. Draft retained." retry:YES];
  } else [self setStatus:@"Save failed. Draft retained." retry:YES];
  return NO;
}
- (void)complete:(id)sender {
  (void)sender;
  if(!_presentationOpen)return;
  [self captureLiveDraft];_completionRequested=true;
  _retryButton.title=@"Retry finish";
  const auto generation=_presentationGeneration;
  const auto reference=_referenceID,mark=_markID;
  const bool validBeforeSave=[self ownerValid];
  if(!_presentationOpen || generation!=_presentationGeneration)return;
  if(!validBeforeSave){[self hide];return;}
  if(![self saveDraft:YES] || !_presentationOpen || generation!=_presentationGeneration ||
     reference!=_referenceID || mark!=_markID || !_completionRequested)return;
  const auto draft=_draft;
  const auto draftGeneration=_draftGeneration;
  const bool validBeforeCopy=[self ownerValid];
  // Validation is an injected callback boundary: its return cannot authorize
  // copying a receipt for a draft/session it changed while running.
  if(!_presentationOpen || generation!=_presentationGeneration || reference!=_referenceID ||
     mark!=_markID || draft!=_draft || draftGeneration!=_draftGeneration || !_completionRequested)return;
  if(!validBeforeCopy){[self hide];return;}
  STNoteCopyStatus copied=STNoteCopyStatus::kUnavailable;
  try {if(_copy)copied=_copy(reference);}catch(...) {copied=STNoteCopyStatus::kFailed;}
  if(!_presentationOpen || generation!=_presentationGeneration || reference!=_referenceID ||
     mark!=_markID || draft!=_draft || draftGeneration!=_draftGeneration || !_completionRequested)return;
  if(copied!=STNoteCopyStatus::kCopied) {
    [self setStatus:copied==STNoteCopyStatus::kUnavailable?
        @"Saved. Copy unavailable; retry finish.":@"Saved. Copy failed; retry finish." retry:YES];
    return;
  }
  _completionRequested=false;_presentationOpen=false;++_presentationGeneration;
  [self stopMouseMonitoring];[_panel orderOut:nil];
}
- (void)retrySave:(id)sender {
  if(_completionRequested)[self complete:sender];else [self saveNow];
}
- (std::string)referenceID {return _referenceID;}
- (std::string)markID {return _markID;}
- (std::uint64_t)presentationGeneration {return _presentationGeneration;}
- (BOOL)presentationOpen {return _presentationOpen;}
- (void)hide {
  if(!_presentationOpen)return;
  [self captureLiveDraft];
  _completionRequested=false;_presentationOpen=false;++_presentationGeneration;
  [self stopMouseMonitoring];
  [self saveNow];[_panel orderOut:nil];
}
- (void)hideIfEditingReference:(const std::string&)referenceID
                         mark:(const std::string&)markID {
  if(_presentationOpen && _referenceID==referenceID && _markID==markID)
    [self hide];
}
- (void)discardOwner {
  // Deletion/expiry are terminal: never retry or restore these bytes.
  [_saveTimer invalidate];_saveTimer=nil;
  _unsavedDrafts.erase([self draftKey]);_dirty=false;
  _completionRequested=false;_presentationOpen=false;++_presentationGeneration;
  [self stopMouseMonitoring];[_panel orderOut:nil];
}
- (BOOL)windowShouldClose:(id)sender {(void)sender;return NO;}
@end

// This production boundary is shared with native tests. It publishes the same
// MarkController observations used by ink/badges, then reconciles the explicitly
// opened pair against uncached metadata. Inspector selection is never authority.
struct STNoteVisibilitySource {
  seethis::core::MarkController* marks = nullptr;
  std::function<std::optional<seethis::core::ReferenceJobSnapshot>(std::string_view)> lookup;
  std::function<seethis::core::MarkNotesSnapshot(std::string_view)> snapshots;
  std::function<seethis::core::ReferenceCollection(std::string_view)> collection;
  std::function<std::vector<seethis::core::FrozenDisplay>()> displays;
  std::function<std::int64_t()> foregroundPID;
  std::function<bool()> screenReady;
  std::function<seethis::core::GlobalVisibilitySnapshot()> visibility;
};
static bool STNoteReferenceAvailable(
    const seethis::core::ReferenceJobSnapshot& job,
    const seethis::core::GlobalVisibilitySnapshot& visibility,
    const std::vector<seethis::core::FrozenDisplay>& displays) {
  using namespace seethis::core;
  return visibility.permits_presentation() &&
      job.state!=ReferenceJobState::kDeleted &&
      job.state!=ReferenceJobState::kExpired &&
      job.state!=ReferenceJobState::kFailed &&
      DisplayTopologyMatches(job.context.displays,displays);
}
static bool STSameWindowIdentity(const seethis::core::WindowObservation& first,
                                 const seethis::core::WindowObservation& second) {
  return first.title.version==second.title.version &&
      first.title.availability==second.title.availability &&
      first.window_title==second.window_title &&
      first.native_window.version==second.native_window.version &&
      first.native_window.process_start_identity_us==second.native_window.process_start_identity_us &&
      first.native_window.observation_session==second.native_window.observation_session &&
      first.native_window.incarnation==second.native_window.incarnation &&
      first.availability==second.availability && first.pid==second.pid &&
      first.bundle_id==second.bundle_id && first.window_id==second.window_id &&
      first.bounds.x==second.bounds.x && first.bounds.y==second.bounds.y &&
      first.bounds.width==second.bounds.width && first.bounds.height==second.bounds.height;
}
struct STInspectorNoteAdmission {std::string root,mark;std::uint64_t generation;};
@interface STNoteVisibilityCoordinator : NSObject {
  STNoteVisibilitySource _source;
  STMarkNoteEditor* _editor;
  std::uint64_t _admittedGeneration;
  std::vector<STInspectorNoteAdmission> _inspectorAdmissions;
}
- (instancetype)initWithEditor:(STMarkNoteEditor*)editor source:(STNoteVisibilitySource)source;
- (BOOL)validateReference:(const std::string&)referenceID mark:(const std::string&)markID topology:(BOOL)topology;
- (BOOL)openInspectorReference:(const std::string&)referenceID mark:(const std::string&)markID
                       number:(std::uint32_t)number screenPoint:(NSPoint)point;
- (void)reconcile;
- (void)retire;
- (void)applicationActivated:(std::int64_t)pid;
- (void)publishWindow:(seethis::core::WindowObservation)observation;
- (void)publishPage:(seethis::core::PageObservation)observation;
- (BOOL)acceptPage:(seethis::core::PageObservation)observation
          request:(seethis::platform::ChromeObservationRequest)request
        lifecycle:(seethis::platform::ChromeObservationLifecycle&)lifecycle
       foreground:(const seethis::core::WindowObservation&)foreground
       permission:(std::uint64_t)permission currentPermission:(std::uint64_t)currentPermission;
- (void)toggleReference:(const std::string&)referenceID mark:(const std::string&)markID
                number:(std::uint32_t)number screenPoint:(NSPoint)point;
@end
// Normal AppKit window levels keep our controls above marks; the same visible
// panel rectangles exclude every mark hit and input-ownership decision.
static bool STOwnPanelCoversPoint(seethis::core::DisplayPoint point,
    const std::vector<seethis::core::FrozenDisplay>& displays,
    const std::vector<seethis::core::Rect>& panels) {
  if(!point.IsValid())return true;
  const auto display=std::find_if(displays.begin(),displays.end(),[&](const auto& value){return value.id==point.display_id;});
  if(display==displays.end())return true;
  const double x=display->logical.x+point.position.x,y=display->logical.y+point.position.y;
  return std::any_of(panels.begin(),panels.end(),[&](const auto& panel) {
    return x>=panel.x&&x<=panel.x+panel.width&&y>=panel.y&&y<=panel.y+panel.height;
  });
}
static bool STOwnPanelKeyboardInput(bool noteOpen,bool noteVisible,bool noteKey,
    bool inspectorVisible,bool inspectorKey) {
  // Nonactivating panels can own the key responder while the original source
  // remains the foreground application. Only these exact owned panels qualify.
  return (noteOpen&&noteVisible&&noteKey)||(inspectorVisible&&inspectorKey);
}
static void STBeginOwnUIPresentation(seethis::core::MarkController& marks) {
  (void)marks.BeginOwnUIPresentationSource();
  marks.SetOwnInputFocus(true);
}

static bool STPublishOwnUIPresentation(seethis::core::MarkController& marks,
    const seethis::core::OwnUIPresentationSourceTarget& target,
    seethis::core::WindowObservation window,seethis::core::PageObservation page,
    bool owned,std::uint64_t permission,std::uint64_t currentPermission) {
  const auto active=marks.own_ui_presentation_source();
  if(!active||active->generation!=target.generation)return false;
  if(!owned||permission!=currentPermission){marks.RetireLiveShot();return false;}
  return marks.PublishOwnUIPresentationObservation(target,std::move(window),std::move(page));
}

@implementation STNoteVisibilityCoordinator
- (instancetype)initWithEditor:(STMarkNoteEditor*)editor source:(STNoteVisibilitySource)source {
  self=[super init];if(self){
    _editor=editor;_source=std::move(source);
    __weak STNoteVisibilityCoordinator* weakSelf=self;
    [_editor setOwnerValidation:[weakSelf](std::string_view reference,std::string_view mark) {
      STNoteVisibilityCoordinator* owner=weakSelf;
      if(owner)[owner reconcile];
      return owner && [owner validReference:std::string(reference) mark:std::string(mark)];
    }];
  }return self;
}
- (BOOL)ownsKeyFocus {
  return _editor.panel.isVisible && _editor.panel.isKeyWindow &&
      NSApp.keyWindow==_editor.panel &&
      _source.foregroundPID()==NSProcessInfo.processInfo.processIdentifier &&
      _admittedGeneration==[_editor presentationGeneration];
}
- (BOOL)validReference:(const std::string&)referenceID mark:(const std::string&)markID {
  const bool local=_editor.presentationOpen && std::any_of(_inspectorAdmissions.begin(),_inspectorAdmissions.end(),
      [&](const auto& admission){return admission.root==referenceID&&admission.mark==markID&&
          admission.generation==[_editor presentationGeneration];});
  return [self validateReference:referenceID mark:markID topology:!local];
}
- (BOOL)validateReference:(const std::string&)referenceID mark:(const std::string&)markID topology:(BOOL)topology {
  using namespace seethis::core;
  const auto visibility=_source.visibility ? _source.visibility() : GlobalVisibilitySnapshot{};
  if(!visibility.permits_presentation() || !_source.screenReady())return NO;
  const auto job=_source.lookup(referenceID);
  if(!job || job->id!=referenceID || job->state==ReferenceJobState::kDeleted ||
     job->state==ReferenceJobState::kExpired || job->state==ReferenceJobState::kFailed)return NO;
  const auto snapshot=_source.snapshots(referenceID);
  if(snapshot.status!=MarkNotesStatus::kReady && snapshot.status!=MarkNotesStatus::kPending)
    return NO;
  const auto mark=std::find_if(snapshot.marks.begin(),snapshot.marks.end(),
      [&](const auto& item){return item.id==markID;});
  if(mark==snapshot.marks.end() || !mark->anchor.IsValid() || mark->number!=mark->slot+1 ||
     mark->id!=StableMarkId(referenceID,mark->slot))return NO;
  auto owner=*job;
  if(_source.collection && job->state==ReferenceJobState::kReady) {
    const auto collection=_source.collection(referenceID);
    if(collection.status!=MarkNotesStatus::kReady)return NO;
    const auto shot=std::find_if(collection.shots.begin(),collection.shots.end(),[&](const auto& item) {
      return item.id==mark->shot_id;
    });
    if(shot==collection.shots.end()||!shot->metadata||shot->metadata->id!=referenceID||
       mark->slot!=shot->first_mark_slot+mark->local_slot||
       mark->local_slot>=EffectiveRegions(*shot->metadata).size())return NO;
    owner.context=shot->metadata->context;owner.page_identity=shot->metadata->page_identity;
  } else if(!mark->shot_id.empty()&&mark->shot_id!=referenceID)return NO;
  return !topology || STNoteReferenceAvailable(owner,visibility,_source.displays());
}
- (BOOL)eligibleReference:(const std::string&)referenceID mark:(const std::string&)markID
                ownFocus:(BOOL)ownFocus {
  using namespace seethis::core;
  if(![self validateReference:referenceID mark:markID topology:YES])return NO;
  const auto job=_source.lookup(referenceID);
  const auto snapshot=_source.snapshots(referenceID);
  const auto selected=std::find_if(snapshot.marks.begin(),snapshot.marks.end(),[&](const auto& item){return item.id==markID;});
  if(selected!=snapshot.marks.end()&&_source.marks->IsShotAdmitted(referenceID,selected->shot_id))return YES;
  if(_source.collection && job->state==ReferenceJobState::kReady) {
    const auto collection=_source.collection(referenceID);
    if(collection.status!=MarkNotesStatus::kReady || collection.multi())return NO;
  }
  if(ReferenceVisible(job->context,job->page_identity,
        _source.marks->window_observation(),_source.marks->page_observation()))return YES;
  // A genuine key note panel may temporarily own the app focus. This is local
  // presentation permission only; no historical source tuple becomes fresh.
  return ownFocus && (job->context.bundle_id=="com.google.Chrome" ||
      CapturedTitleAvailable(job->context)) && _source.marks->window_observation().availability==
      WindowAvailability::kUnavailable;
}
- (BOOL)openInspectorReference:(const std::string&)referenceID mark:(const std::string&)markID
                       number:(std::uint32_t)number screenPoint:(NSPoint)point {
  const auto job=_source.lookup(referenceID);
  if(!job||job->state!=seethis::core::ReferenceJobState::kReady||
     ![self validateReference:referenceID mark:markID topology:NO])return NO;
  const auto notes=_source.snapshots(referenceID);
  const auto mark=std::find_if(notes.marks.begin(),notes.marks.end(),[&](const auto& item){return item.id==markID;});
  if(notes.status!=seethis::core::MarkNotesStatus::kReady||mark==notes.marks.end()||mark->number!=number)return NO;
  // Preserve the current owner's admission until save-before-switch succeeds.
  std::erase_if(_inspectorAdmissions,[&](const auto& item){return !_editor.presentationOpen||
      item.generation!=[_editor presentationGeneration]||item.root!=[_editor referenceID]||item.mark!=[_editor markID];});
  const bool same=_editor.presentationOpen&&[_editor referenceID]==referenceID&&[_editor markID]==markID;
  const auto generation=[_editor presentationGeneration]+(same?0:1);
  std::erase_if(_inspectorAdmissions,[&](const auto& item){return item.root==referenceID&&item.mark==markID&&item.generation==generation;});
  _inspectorAdmissions.push_back({referenceID,markID,generation});
  if(_source.marks)STBeginOwnUIPresentation(*_source.marks);
  [_editor openReference:referenceID mark:markID number:number screenPoint:point];
  std::erase_if(_inspectorAdmissions,[&](const auto& item){return !_editor.presentationOpen||
      item.generation!=[_editor presentationGeneration]||item.root!=[_editor referenceID]||item.mark!=[_editor markID];});
  _admittedGeneration=[_editor presentationGeneration];
  [self reconcile];
  return _editor.presentationOpen&&[_editor referenceID]==referenceID&&[_editor markID]==markID;
}
- (void)retire {
  _admittedGeneration=0;
  [_editor hide];_inspectorAdmissions.clear();
}
- (void)reconcile {
  if(!_editor.presentationOpen)return;
  const auto reference=[_editor referenceID],mark=[_editor markID];
  const auto job=_source.lookup(reference);
  bool dead=!job || job->state==seethis::core::ReferenceJobState::kDeleted ||
      job->state==seethis::core::ReferenceJobState::kExpired;
  if(!dead) {
    const auto snapshot=_source.snapshots(reference);
    if(snapshot.status==seethis::core::MarkNotesStatus::kReady ||
       snapshot.status==seethis::core::MarkNotesStatus::kPending)
      dead=std::none_of(snapshot.marks.begin(),snapshot.marks.end(),
          [&](const auto& item){return item.id==mark;});
  }
  if(dead){_admittedGeneration=0;[_editor discardOwner];return;}
  if(![self validReference:reference mark:mark])[self retire];
}
- (void)applicationActivated:(std::int64_t)pid {
  (void)pid;[self reconcile];
}
- (void)publishWindow:(seethis::core::WindowObservation)observation {
  if(observation.generation<=_source.marks->window_observation().generation)return;
  _source.marks->SetWindowObservation(std::move(observation));
  [self reconcile];
}
- (void)publishPage:(seethis::core::PageObservation)observation {
  if(observation.generation<=_source.marks->page_observation().generation)return;
  _source.marks->SetPageObservation(std::move(observation));
  [self reconcile];
}
- (BOOL)acceptPage:(seethis::core::PageObservation)observation
          request:(seethis::platform::ChromeObservationRequest)request
        lifecycle:(seethis::platform::ChromeObservationLifecycle&)lifecycle
       foreground:(const seethis::core::WindowObservation&)foreground
       permission:(std::uint64_t)permission currentPermission:(std::uint64_t)currentPermission {
  if(!lifecycle.Accepts(request) || permission!=currentPermission ||
     observation.generation!=request.generation ||
     observation.generation<=_source.marks->page_observation().generation)return NO;
  if(!STSameWindowIdentity(foreground,_source.marks->window_observation())) {
    os_log_with_type(OS_LOG_DEFAULT,OS_LOG_TYPE_INFO,
        "seethis chrome phase=callback-validation cause=window-changed generation=%{public}llu pid=%{public}lld window=%{public}llu",
        static_cast<unsigned long long>(request.generation),
        static_cast<long long>(foreground.pid),
        static_cast<unsigned long long>(foreground.window_id));
    return NO;
  }
  lifecycle.Complete(request);
  [self publishPage:std::move(observation)];
  return YES;
}
- (void)toggleReference:(const std::string&)referenceID mark:(const std::string&)markID
                number:(std::uint32_t)number screenPoint:(NSPoint)point {
  if(_editor.presentationOpen && [_editor referenceID]==referenceID &&
     [_editor markID]==markID) {
    [self reconcile];
    if(_editor.presentationOpen && [self eligibleReference:referenceID mark:markID ownFocus:[self ownsKeyFocus]])
      [_editor complete:nil];
    return;
  }
  // A cached badge hit cannot resurrect a deleted, hidden, or stale owner.
  if(![self eligibleReference:referenceID mark:markID ownFocus:NO])return;
  _admittedGeneration=[_editor presentationGeneration]+1;
  STBeginOwnUIPresentation(*_source.marks);
  [_editor toggleReference:referenceID mark:markID number:number screenPoint:point];
  _admittedGeneration=[_editor presentationGeneration];
}
@end

struct STNoteBadge {
  std::string reference_id;
  std::string mark_id;
  std::uint32_t number = 0;
  seethis::core::DisplayPoint anchor;
};
static std::vector<STNoteBadge> STReferenceNoteBadges(
    const std::string& id,const seethis::core::MarkNotesSnapshot& snapshot,
    const seethis::core::GlobalVisibilitySnapshot& visibility) {
  using namespace seethis::core;
  std::vector<STNoteBadge> badges;
  if(!visibility.permits_presentation() ||
     (snapshot.status!=MarkNotesStatus::kReady && snapshot.status!=MarkNotesStatus::kPending))
    return badges;
  for(const auto& mark:snapshot.marks)
    if(mark.anchor.IsValid() && mark.id==StableMarkId(id,mark.slot))
      badges.push_back({id,mark.id,mark.number,mark.anchor});
  return badges;
}
static NSRect STNoteBadgeRect(const seethis::core::DisplayPoint& anchor) {
  if(!anchor.IsValid())return NSZeroRect;
  const auto logical=seethis::core::ConvertCoordinateUnit(
      anchor,seethis::core::CoordinateUnit::kLogicalPoints);
  if(!logical)return NSZeroRect;
  return NSMakeRect(logical->position.x-13,logical->position.y-13,26,26);
}
static bool STNoteBadgeHit(const STNoteBadge& badge,
                          const seethis::core::DisplayPoint& point) {
  if(!badge.anchor.IsValid()||badge.anchor.display_id!=point.display_id)
    return false;
  const auto logical=seethis::core::ConvertCoordinateUnit(
      point,seethis::core::CoordinateUnit::kLogicalPoints);
  if(!logical)return false;
  const NSRect rect=STNoteBadgeRect(badge.anchor);
  const CGFloat dx=logical->position.x-NSMidX(rect);
  const CGFloat dy=logical->position.y-NSMidY(rect);
  return dx*dx+dy*dy<=13*13;
}

// The same NSView and event ordering run in the app and the native event test.
@protocol STOverlayRouting <NSObject>
- (void)paintDisplay:(seethis::core::DisplayId)display;
- (BOOL)handleNoteBadgeClick:(seethis::core::DisplayPoint)point;
- (BOOL)click:(seethis::core::DisplayPoint)point;
- (BOOL)beginRegion:(seethis::core::DisplayPoint)point;
- (void)moveRegion:(seethis::core::DisplayPoint)point;
- (void)endRegion:(seethis::core::DisplayPoint)point;
@end
@interface STOverlayView : NSView
@property(nonatomic,weak) id<STOverlayRouting> coordinator;
@property(nonatomic) seethis::core::DisplayId displayID;
@property(nonatomic) BOOL claimedDeleteSequence;
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
  // A number or existing stroke must handle the down event before drawing can
  // claim it. The release path remains safe when no region was started.
  self.claimedDeleteSequence=[self.coordinator handleNoteBadgeClick:point]||
      [self.coordinator click:point]||[self.coordinator beginRegion:point];
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
std::vector<seethis::core::ReferenceRegion> STVisibleReferenceRegions(
    const seethis::core::Reference& reference,
    const seethis::core::MarkNotesSnapshot& snapshot,
    const seethis::core::GlobalVisibilitySnapshot& visibility) {
  const auto regions=seethis::core::EffectiveRegions(reference);
  std::vector<seethis::core::ReferenceRegion> visible;
  // If metadata cannot be read, do not accidentally reveal a hidden stroke.
  if(!visibility.permits_presentation() ||
     snapshot.status!=seethis::core::MarkNotesStatus::kReady ||
     snapshot.marks.size()<reference.first_mark_slot+regions.size())return visible;
  for(std::size_t slot=0;slot<regions.size();++slot)
    if(reference.first_mark_slot+slot<snapshot.marks.size() &&
       snapshot.marks[reference.first_mark_slot+slot].slot==reference.first_mark_slot+slot &&
       snapshot.marks[reference.first_mark_slot+slot].id==seethis::core::StableMarkId(reference.id,reference.first_mark_slot+slot))
      visible.push_back(regions[slot]);
  return visible;
}
static void STObservePresentationInput(seethis::platform::ViewportContinuity& continuity,
    seethis::core::MarkController& marks,seethis::platform::ViewportInput event,
    bool own,bool preservesPresentation) {
  continuity.Observe(event,own||preservesPresentation);
  marks.SetViewportContinuity(continuity.epoch(),continuity.healthy());
}
static bool STSourceFocusPermissionCurrent(const seethis::core::Context& source,
    std::uint64_t expected,std::uint64_t current) {
  return source.bundle_id!="com.google.Chrome"||expected==current;
}
// Presentation continuity only: this never claims input or creates admission.
// Judge current eligibility before invoking either fresh native query.
template<class Destination,class Source>
static bool STSourceFocusPreservesPresentation(
    seethis::platform::ViewportInput event,
    const std::optional<seethis::core::DisplayPoint>& point,bool healthy,
    seethis::core::ReferenceStore& references,seethis::core::MarkController& marks,
    const seethis::core::GlobalVisibilitySnapshot& visibility,
    const std::vector<seethis::core::FrozenDisplay>& displays,
    const std::vector<std::uint64_t>& passThroughPanels,
    Destination destinationAt,Source observeSource) {
  using namespace seethis::core;
  if(event!=seethis::platform::ViewportInput::kPrimaryDown||!healthy||!point||
     !point->IsValid()||point->unit!=CoordinateUnit::kLogicalPoints||
     !visibility.permits_presentation())return false;
  const auto display=std::find_if(displays.begin(),displays.end(),[&](const auto& d){return d.id==point->display_id;});
  if(display==displays.end()||point->backing_scale!=display->scale||
     point->position.x<0||point->position.y<0||point->position.x>=display->logical.width||
     point->position.y>=display->logical.height)return false;
  std::vector<Reference> eligible;
  for(const auto& item:marks.marks()) {
    const auto& ref=item.value;
    if(ref.context.native_window.version==1&&DisplayTopologyMatches(ref.context.displays,displays)&&
       marks.PresentedReferenceVisible(ref)&&
       !STVisibleReferenceRegions(ref,references.MarkSnapshots(ref.id),visibility).empty())eligible.push_back(ref);
  }
  if(eligible.empty())return false;
  std::uint64_t destination=0,below=0;
  // Only our exact pass-through panels may be skipped. An unknown/accepting
  // surface is a destination, never permission to look through another app.
  for(std::size_t count=0;count<=passThroughPanels.size();++count) {
    destination=destinationAt(below);
    if(!destination||destination==below)return false;
    if(std::find(passThroughPanels.begin(),passThroughPanels.end(),destination)==passThroughPanels.end())break;
    below=destination;destination=0;
  }
  if(!destination)return false;
  for(const auto& ref:eligible) {
    if(destination!=ref.context.window_id)continue;
    const auto fresh=observeSource(ref.context);
    if(!WindowObservationMatchesContext(ref.context,fresh)||
       !marks.PresentedReferenceVisible(ref))continue;
    if(ref.context.bundle_id=="com.google.Chrome") {
      if(!ref.page_identity)continue; // Existing predicate owns page freshness.
    } else if(!TitleContextVisible(ref.context,fresh))continue;
    return true;
  }
  return false;
}
bool STRegionsHit(const seethis::core::Context& context,
                  const std::vector<seethis::core::ReferenceRegion>& regions,
                  seethis::core::DisplayPoint point,double radius,
                  const std::vector<seethis::core::FrozenDisplay>& displays) {
  if(!point.IsValid() ||
     point.unit!=seethis::core::CoordinateUnit::kLogicalPoints ||
     !std::isfinite(radius) || radius<0 ||
     !seethis::core::DisplayTopologyMatches(context.displays,displays))return false;
  for(const auto& region:regions) {
    bool hit=false;
    seethis::core::ForEachSubpath(region,[&](const auto& path) {
      for(std::size_t index=0;index<path.size();++index) {
        const auto& sample=path[index];
        if(sample.display_id!=point.display_id ||
           sample.backing_scale!=point.backing_scale)continue;
        const auto& previous=index>0&&path[index-1].display_id==sample.display_id?
            path[index-1]:sample;
        const double dx=sample.position.x-previous.position.x;
        const double dy=sample.position.y-previous.position.y;
        const double length=dx*dx+dy*dy;
        const double t=length>0?std::clamp(
            ((point.position.x-previous.position.x)*dx+
             (point.position.y-previous.position.y)*dy)/length,0.0,1.0):0.0;
        if(std::hypot(point.position.x-previous.position.x-t*dx,
                      point.position.y-previous.position.y-t*dy)<=radius)
          hit=true;
      }
    });
    if(hit)return true;
  }
  return false;
}
// A stroke hit keeps its region identity until selection; a root-only hover is
// insufficient to choose which mark owns the next capture.
struct STStrokeHit {
  std::string rootID,shotID,markID;
  std::size_t localSlot=0,globalSlot=0;
  unsigned number=0;
};
static std::optional<STStrokeHit> STVisibleStrokeHit(
    seethis::core::ReferenceStore& references,seethis::core::MarkController& marks,
    const seethis::core::GlobalVisibilitySnapshot& visibility,
    const std::vector<seethis::core::FrozenDisplay>& displays,
    seethis::core::DisplayPoint point,double radius,
    const std::vector<seethis::core::Rect>& ownPanels={}) {
  using namespace seethis::core;
  if((!ownPanels.empty()&&STOwnPanelCoversPoint(point,displays,ownPanels))||!marks.PresentationHitsAllowed()||!visibility.permits_presentation() || !point.IsValid() ||
     point.unit!=CoordinateUnit::kLogicalPoints || !std::isfinite(radius) || radius<0)return {};
  const auto jobs=references.Jobs();
  for(auto it=jobs.rbegin();it!=jobs.rend();++it) {
    if(it->state==ReferenceJobState::kDeleted || it->state==ReferenceJobState::kExpired)continue;
    Reference reference;
    std::string shotID=it->id;
    if(it->state==ReferenceJobState::kReady) {
      const auto collection=references.Collection(it->id);
      if(collection.status!=MarkNotesStatus::kReady)continue;
      auto candidates=collection.multi()?marks.LiveShots(it->id):std::vector<ReferenceShot>{};
      if(!collection.multi()) {
        if(!it->metadata||!marks.PresentedReferenceVisible(*it->metadata))continue;
        candidates.push_back({it->id,0,it->metadata});
      }
      const auto snapshot=references.MarkSnapshots(it->id);
      if(snapshot.status!=MarkNotesStatus::kReady)continue;
      // Newer admitted shots paint above older ones; local ties keep old order.
      for(auto shot=candidates.rbegin();shot!=candidates.rend();++shot) {
        reference=*shot->metadata;shotID=shot->id;
        const auto regions=EffectiveRegions(reference);
        if(snapshot.marks.size()<reference.first_mark_slot+regions.size())continue;
        for(std::size_t local=0;local<regions.size();++local) {
          const auto global=reference.first_mark_slot+local;
          const auto& mark=snapshot.marks[global];
          if(mark.slot!=global || mark.id!=StableMarkId(it->id,global) ||
             mark.number!=global+1 || mark.shot_id!=shotID || mark.local_slot!=local)continue;
          if(STRegionsHit(reference.context,{regions[local]},point,radius,displays))
            return STStrokeHit{it->id,shotID,mark.id,local,global,mark.number};
        }
      }
      continue;
    }
    // Pending/failed strokes retain their existing recopy and delete behavior,
    // but cannot synthesize a durable append target.
    if(!ReferenceVisible(it->context,it->page_identity,
        marks.window_observation(),marks.page_observation()))continue;
    if(it->metadata)reference=*it->metadata;
    else if(it->ready)reference=it->ready->value;
    else {reference.id=it->id;reference.context=it->context;
      reference.path=it->path;reference.regions=it->regions;}
    if(STRegionsHit(reference.context,EffectiveRegions(reference),point,radius,displays))
      return STStrokeHit{it->id};
  }
  return {};
}
static std::vector<STNoteBadge> STPresentedNoteBadges(
    seethis::core::ReferenceStore& references,seethis::core::MarkController& marks,
    const std::string& root,const seethis::core::GlobalVisibilitySnapshot& visibility,
    const std::vector<seethis::core::FrozenDisplay>& displays) {
  using namespace seethis::core;
  if(!visibility.permits_presentation())return {};
  auto snapshot=references.MarkSnapshots(root);const auto collection=references.Collection(root);
  if(collection.status!=MarkNotesStatus::kReady)return {};
  if(collection.multi()) {
    const auto live=marks.LiveShots(root);
    std::erase_if(snapshot.marks,[&](const auto& mark) {
      return std::none_of(live.begin(),live.end(),[&](const auto& shot) {
        return shot.id==mark.shot_id&&DisplayTopologyMatches(shot.metadata->context.displays,displays);
      });
    });
  } else {
    const auto job=references.LookupMetadata(root);
    if(!job||!(job->metadata?marks.PresentedReferenceVisible(*job->metadata):
        ReferenceVisible(job->context,job->page_identity,marks.window_observation(),marks.page_observation()))||
       !DisplayTopologyMatches(job->context.displays,displays))return {};
  }
  return STReferenceNoteBadges(root,snapshot,visibility);
}
static bool STOwnStrokeSelectionInput(bool acceptingOverlayDestination,
    seethis::core::ReferenceStore& references,seethis::core::MarkController& marks,
    const seethis::core::GlobalVisibilitySnapshot& visibility,
    const std::vector<seethis::core::FrozenDisplay>& displays,
    seethis::core::DisplayPoint point,double radius,
    const std::vector<seethis::core::Rect>& ownPanels={}) {
  if(!acceptingOverlayDestination)return false;
  const auto hit=STVisibleStrokeHit(references,marks,visibility,displays,point,radius,ownPanels);
  return hit && !hit->markID.empty();
}
static void STSelectStrokeHit(const STStrokeHit& hit,
    seethis::platform::InspectorLifecycleState& lifecycle,std::string& selectedMark,
    std::string& previewRoot,std::string& previewShot) {
  if(hit.rootID!=lifecycle.selected_id)selectedMark.clear();
  lifecycle=seethis::platform::ReduceInspectorLifecycle(lifecycle,
      seethis::platform::InspectorLifecycleEvent::kReferenceSelection,hit.rootID);
  if(!hit.markID.empty()) {
    selectedMark=hit.markID;previewRoot=hit.rootID;previewShot=hit.shotID;
  }
}
struct STCaptureSelectionStart {
  std::string rootID;
  std::uint64_t revision=0,generation=0,targetGeneration=0;
};
// Both the native shortcut and the no-show component test use this boundary.
// Values are copied before starting asynchronous work and cannot be retargeted.
static STCaptureSelectionStart STBeginModeCapture(
    seethis::core::MarkController& marks,seethis::core::ReferenceStore& references,
    seethis::platform::AppendCaptureMode mode,std::string operation,
    double margin,std::int64_t keyDown) {
  STCaptureSelectionStart start;start.targetGeneration=mode.generation;
  if(!mode.enabled()) {
    start.rootID=operation;start.generation=marks.Begin(std::move(operation),margin,keyDown);
  } else {
    start.rootID=mode.root;start.revision=references.Collection(start.rootID).revision;
    start.generation=marks.BeginAppendReference(start.rootID,start.revision,
        std::move(operation),margin,keyDown);
  }
  return start;
}
static std::optional<std::string> STValidateAppendMode(
    seethis::platform::AppendCaptureMode& mode,seethis::core::ReferenceStore& references,
    seethis::core::MarkController& marks) {
  if(!mode.enabled()||references.Collection(mode.root).status==seethis::core::MarkNotesStatus::kReady)return {};
  const auto previous=mode.root;mode.Disable();marks.RetireReferencePresentation(previous);return previous;
}
// The native checkbox and no-show component tests share the full mode,
// selection-generation and refresh dispatch, including failed-enable refresh.
template<class Refresh>
static void STToggleInspectorAppendMode(seethis::platform::AppendCaptureMode& mode,
    seethis::core::ReferenceStore& references,seethis::platform::InspectorLifecycleState& lifecycle,
    Refresh&& refresh) {
  if(mode.enabled())mode.Disable();
  else if(!mode.Enable(lifecycle.selected_id,references.Collection(lifecycle.selected_id).status==seethis::core::MarkNotesStatus::kReady)) {
    refresh(@"Select a ready reference before enabling append.");return;
  }
  ++lifecycle.selection_generation;refresh(nil);
}
// These are the production transition/observation boundaries, also exercised by
// no-show tests. A callback may synchronously cancel or replace this attempt.
template<class Begin,class Finish,class Cancel>
static void STRefreshCaptureTransition(
    seethis::core::InteractionController& interaction,
    seethis::core::InteractionState& previous,const std::uint64_t& generation,
    Begin&& begin,Finish&& finish,Cancel&& cancel) {
  using seethis::core::InteractionState;
  const auto snapshot=interaction.Snapshot();
  auto ownedGeneration=generation;
  if(snapshot.state==InteractionState::kDrawing&&previous!=InteractionState::kDrawing)
    begin(snapshot,ownedGeneration);
  else if(snapshot.state==InteractionState::kFinished&&previous==InteractionState::kDrawing)
    finish(snapshot);
  else if(snapshot.state==InteractionState::kIdle&&previous==InteractionState::kDrawing)
    cancel(snapshot);
  // A nested refresh may already own a newer generation. Otherwise commit the
  // live state, never the pre-callback Drawing snapshot.
  if(generation==ownedGeneration)previous=interaction.Snapshot().state;
}
template<class Observe,class Schedule>
static bool STObserveAndScheduleCapture(
    seethis::core::InteractionController& interaction,seethis::core::MarkController& marks,
    const std::uint64_t& currentGeneration,std::uint64_t capturedGeneration,
    Observe&& observe,Schedule&& schedule) {
  observe();
  if(currentGeneration!=capturedGeneration||
     !interaction.Snapshot().captures_pointer()||!marks.Pending()||!marks.AllowsDrawing())return false;
  schedule();return true;
}
static bool STCompleteCaptureAttempt(seethis::core::MarkController& marks,
    const std::uint64_t& currentGeneration,std::uint64_t deliveredGeneration,
    seethis::core::CaptureResult result,bool& succeeded,bool& selectionPending) {
  if(deliveredGeneration==currentGeneration) {
    succeeded=result.error.empty()&&seethis::core::ValidPixels(result.pixels);
    if(!succeeded)selectionPending=false;
  }
  marks.Captured(deliveredGeneration,std::move(result));
  return deliveredGeneration==currentGeneration&&!succeeded;
}
}

// The document body owns marks and preview geometry; reference selection lives
// exclusively in STInspectorReferenceSelector above the scroll view.
struct STInspectorBodyLayout {
  std::vector<NSRect> markRowFrames;
  CGFloat controlsTop;
  CGFloat textTop;
  NSRect previewFrame;
  CGFloat documentHeight;
};
static NSString* InspectorMarkNoteText(const seethis::core::MarkSnapshot& mark) {
  NSString* note=[[NSString alloc] initWithBytes:mark.note.data()
      length:mark.note.size() encoding:NSUTF8StringEncoding];
  return note?:@"";
}
static NSString* InspectorMarkRowText(const seethis::core::MarkSnapshot& mark) {
  NSString* note=InspectorMarkNoteText(mark);
  return note.length?[NSString stringWithFormat:@"%u  %@",mark.number,note]:
      [NSString stringWithFormat:@"%u",mark.number];
}
static CGFloat InspectorMarkRowHeight(NSString* row,CGFloat width) {
  const NSRect text=[row boundingRectWithSize:NSMakeSize(MAX(1,width-48),CGFLOAT_MAX)
      options:NSStringDrawingUsesLineFragmentOrigin|NSStringDrawingUsesFontLeading
      attributes:@{NSFontAttributeName:[NSFont systemFontOfSize:13]}];
  return MAX(28,ceil(NSHeight(text))+12);
}
static STInspectorBodyLayout InspectorBodyLayout(CGFloat width,
    CGFloat viewportHeight,CGFloat textHeight,NSArray<NSString*>* markRows,
    BOOL selected) {
  const NSRect previewFrame=NSMakeRect(16,32,MAX(1,width-32),180);
  std::vector<NSRect> markRowFrames;
  CGFloat nextRow=NSMaxY(previewFrame)+30;
  for(NSString* row in markRows) {
    const CGFloat height=InspectorMarkRowHeight(row,width);
    markRowFrames.push_back(NSMakeRect(8,nextRow,MAX(1,width-16),height));
    nextRow+=height+4;
  }
  const CGFloat controlsTop=markRows.count?nextRow+8:NSMaxY(previewFrame)+12;
  const CGFloat textTop=controlsTop+(selected?92:4);
  return {std::move(markRowFrames),controlsTop,textTop,previewFrame,
      MAX(viewportHeight,textTop+ceil(textHeight)+20)};
}
static NSInteger InspectorMarkRowAtPoint(NSPoint point,
    const STInspectorBodyLayout& layout) {
  for(std::size_t index=0;index<layout.markRowFrames.size();++index)
    if(NSPointInRect(point,layout.markRowFrames[index]))return index;
  return NSNotFound;
}
enum class STInspectorMarkAction { kSelect, kOpenNote };
static STInspectorMarkAction InspectorMarkAction(NSInteger clickCount) {
  return clickCount>=2?STInspectorMarkAction::kOpenNote:STInspectorMarkAction::kSelect;
}
static NSRect InspectorAspectFit(NSSize source,NSRect destination) {
  if(source.width<=0 || source.height<=0 || NSIsEmptyRect(destination))return NSZeroRect;
  const CGFloat scale=MIN(NSWidth(destination)/source.width,
                          NSHeight(destination)/source.height);
  const NSSize fitted=NSMakeSize(source.width*scale,source.height*scale);
  return NSMakeRect(NSMidX(destination)-fitted.width/2,
      NSMidY(destination)-fitted.height/2,fitted.width,fitted.height);
}
// Preview labels use source coordinates, never thumbnail pixels or crop bounds.
// This helper is shared with the isolated AppKit raster test below.
struct STPreviewNumber { unsigned number; NSPoint source; std::string markID; };
struct STPreviewNumbers {
  std::string key;
  NSSize source=NSZeroSize;
  std::string referenceID, shotID, sourceSHA, metadataSHA, digest, geometryVersion;
  std::uint64_t contentRevision=1;
  std::vector<STPreviewNumber> marks;
};
static bool InspectorPreviewCompletionMatches(bool presentationCurrent,bool ready,
    const std::string& selected,const std::string& readyID,
    const std::string& expectedDigest,NSString* digest,NSString* currentKey,NSString* key) {
  return presentationCurrent && ready && selected==readyID &&
      expectedDigest==(digest.UTF8String?:"") && [currentKey isEqualToString:key];
}
struct STPreviewCallout { unsigned number; NSPoint anchor; NSRect bounds; std::vector<NSPoint> leader; };
static STPreviewNumbers InspectorPreviewNumbers(const seethis::core::Reference& ref,
    const seethis::core::MarkNotesSnapshot& snapshot,std::string key,
    bool requireComplete=true) {
  STPreviewNumbers result;
  result.key=std::move(key);
  result.source=NSMakeSize(ref.source.width,ref.source.height);
  result.referenceID=ref.id;
  result.sourceSHA=ref.source.sha256;
  result.metadataSHA=seethis::core::Sha256(seethis::core::Serialize(ref));
  result.geometryVersion=std::string(seethis::core::ReferenceStore::kDisplayUnderlayGeometryVersion);
  if(ref.schema!=2 || ref.source.width==0 || ref.source.height==0 ||
     snapshot.status!=seethis::core::MarkNotesStatus::kReady ||
     (requireComplete && snapshot.marks.size()!=seethis::core::EffectiveRegions(ref).size()))return {};
  std::vector<std::string> ids;
  std::vector<unsigned> numbers;
  for(const auto& mark:snapshot.marks) {
    if(mark.number!=mark.slot+1 || mark.id!=seethis::core::StableMarkId(ref.id,mark.slot) ||
       std::find(ids.begin(),ids.end(),mark.id)!=ids.end() ||
       std::find(numbers.begin(),numbers.end(),mark.number)!=numbers.end() ||
       (requireComplete && (mark.shot_id.empty()?mark.slot:mark.local_slot)>=snapshot.marks.size()))return {};
    ids.push_back(mark.id);numbers.push_back(mark.number);
    const auto display=std::find_if(ref.context.displays.begin(),ref.context.displays.end(),
        [&](const auto& item){return item.id==mark.anchor.display_id;});
    if(display==ref.context.displays.end())return {};
    const NSPoint point=NSMakePoint(ref.global_to_source.a*(display->logical.x+
        mark.anchor.position.x)+ref.global_to_source.tx,
        ref.global_to_source.d*(display->logical.y+mark.anchor.position.y)+
        ref.global_to_source.ty);
    if(!std::isfinite(point.x)||!std::isfinite(point.y)||point.x<0||point.y<0||
       point.x>result.source.width||point.y>result.source.height)return {};
    result.marks.push_back({mark.number,point,mark.id});
  }
  return result;
}
static NSPoint InspectorLeaderEnd(NSPoint anchor,NSRect box) {
  return NSMakePoint(std::clamp(anchor.x,NSMinX(box),NSMaxX(box)),
                     std::clamp(anchor.y,NSMinY(box),NSMaxY(box)));
}
static bool InspectorSegmentHits(NSPoint a,NSPoint b,NSRect box) {
  double low=0,high=1;
  for(int axis=0;axis<2;++axis) {
    const double start=axis?a.y:a.x,delta=axis?b.y-a.y:b.x-a.x;
    const double minimum=axis?NSMinY(box):NSMinX(box);
    const double maximum=axis?NSMaxY(box):NSMaxX(box);
    if(fabs(delta)<1e-9) {if(start<minimum||start>maximum)return false;}
    else {double p=(minimum-start)/delta,q=(maximum-start)/delta;
      if(p>q)std::swap(p,q);low=std::max(low,p);high=std::min(high,q);
      if(low>high)return false;}
  }
  return true;
}
static std::vector<STPreviewCallout> InspectorPreviewCallouts(
    const STPreviewNumbers& data,NSRect viewport) {
  std::vector<STPreviewCallout> result;
  const NSRect fitted=InspectorAspectFit(data.source,viewport);
  if(NSIsEmptyRect(fitted))
    return result;
  std::vector<NSPoint> anchors;
  for(const auto& mark:data.marks)anchors.push_back(NSMakePoint(
      NSMinX(fitted)+mark.source.x*NSWidth(fitted)/data.source.width,
      NSMinY(fitted)+mark.source.y*NSHeight(fitted)/data.source.height));
  NSDictionary* attributes=@{NSFontAttributeName:
      [NSFont monospacedDigitSystemFontOfSize:14 weight:NSFontWeightBold]};
  for(std::size_t i=0;i<data.marks.size();++i) {
    NSString* text=[NSString stringWithFormat:@"%u",data.marks[i].number];
    const CGFloat width=MAX(24,ceil([text sizeWithAttributes:attributes].width)+10);
    const CGFloat height=24;
    NSRect best=NSZeroRect;double score=HUGE_VAL;
    for(CGFloat y=NSMinY(viewport)+2;y+height<=NSMaxY(viewport)-2;y+=4)
      for(CGFloat x=NSMinX(viewport)+2;x+width<=NSMaxX(viewport)-2;x+=4) {
        NSRect box=NSMakeRect(x,y,width,height);
        bool blocked=false;
        for(auto anchor:anchors)if(NSPointInRect(anchor,NSInsetRect(box,-3,-3)))
          {blocked=true;break;}
        const auto end=InspectorLeaderEnd(anchors[i],box);
        for(const auto& previous:result) {
          if(NSIntersectsRect(NSInsetRect(box,-6,-6),previous.bounds))
            {blocked=true;break;}
        }
        if(blocked)continue;
        const double distance=hypot(NSMidX(box)-anchors[i].x,NSMidY(box)-anchors[i].y);
        // Prefer empty letterbox space without sending a nearby leader across the image.
        const double candidate=distance+(NSIntersectsRect(box,fitted)?40:0);
        if(candidate<score){score=candidate;best=box;}
      }
    // A finite viewport has finite capacity. Never silently omit a number.
    if(NSIsEmptyRect(best))return {};
    result.push_back({data.marks[i].number,anchors[i],best,{}});
  }
  // Route leaders through a visibility graph around every label. Placement and
  // routing are separate so adjacent starts cannot trap subsequent labels.
  for(std::size_t i=0;i<result.size();++i) {
    std::vector<NSPoint> nodes={result[i].anchor,InspectorLeaderEnd(result[i].anchor,result[i].bounds)};
    for(std::size_t j=0;j<result.size();++j)if(i!=j) {
      NSRect b=NSInsetRect(result[j].bounds,-3,-3);
      for(auto p:{NSMakePoint(NSMinX(b),NSMinY(b)),NSMakePoint(NSMaxX(b),NSMinY(b)),
          NSMakePoint(NSMaxX(b),NSMaxY(b)),NSMakePoint(NSMinX(b),NSMaxY(b))})
        if(NSPointInRect(p,viewport))nodes.push_back(p);
    }
    std::vector<double> distances(nodes.size(),HUGE_VAL);
    std::vector<int> parents(nodes.size(),-1);std::vector<bool> done(nodes.size(),false);
    distances[0]=0;
    for(std::size_t step=0;step<nodes.size();++step) {
      int next=-1;
      for(std::size_t n=0;n<nodes.size();++n)if(!done[n]&&(next<0||distances[n]<distances[next]))next=n;
      if(next<0||!std::isfinite(distances[next]))break;
      done[next]=true;if(next==1)break;
      for(std::size_t n=0;n<nodes.size();++n)if(!done[n]) {
        bool blocked=false;
        for(std::size_t j=0;j<result.size();++j)if(i!=j&&InspectorSegmentHits(nodes[next],nodes[n],
            NSInsetRect(result[j].bounds,-1,-1))){blocked=true;break;}
        if(blocked)continue;
        const double distance=distances[next]+hypot(nodes[n].x-nodes[next].x,nodes[n].y-nodes[next].y);
        if(distance<distances[n]){distances[n]=distance;parents[n]=next;}
      }
    }
    if(!std::isfinite(distances[1]))return {};
    for(int n=1;n>=0;n=parents[n]){result[i].leader.push_back(nodes[n]);if(n==0)break;}
    std::reverse(result[i].leader.begin(),result[i].leader.end());
  }
  return result;
}
static void InspectorDrawPreview(NSImage* image,NSRect viewport,
    const STPreviewNumbers& data,NSString* key) {
  if(data.key!=(key.UTF8String?:""))return;
  const NSRect fitted=InspectorAspectFit(data.source,viewport);
  [image drawInRect:fitted fromRect:NSZeroRect operation:NSCompositingOperationSourceOver
      fraction:1 respectFlipped:YES hints:nil];
  if(data.key!=(key.UTF8String?:""))return;
  const auto labels=InspectorPreviewCallouts(data,viewport);
  if(!data.marks.empty() && labels.empty()) {
    [@"Preview crowded — open the marked PNG to read all numbers." drawInRect:
        NSInsetRect(viewport,6,6) withAttributes:@{NSFontAttributeName:
        [NSFont systemFontOfSize:13],NSForegroundColorAttributeName:NSColor.labelColor}];
    return;
  }
  for(const auto& label:labels) {
    NSBezierPath* line=[NSBezierPath bezierPath];
    [line moveToPoint:label.anchor];
    for(std::size_t n=1;n<label.leader.size();++n)[line lineToPoint:label.leader[n]];
    [NSColor.whiteColor setStroke];line.lineWidth=4;[line stroke];
    [[NSColor colorWithCalibratedRed:0.68 green:0.02 blue:0.14 alpha:1] setStroke];
    line.lineWidth=2;[line stroke];
    [[NSColor colorWithCalibratedRed:0.68 green:0.02 blue:0.14 alpha:1] setFill];
    [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(label.anchor.x-2,
        label.anchor.y-2,4,4)] fill];
  }
  for(const auto& label:labels) {
    NSBezierPath* badge=[NSBezierPath bezierPathWithRoundedRect:label.bounds xRadius:6 yRadius:6];
    [[NSColor colorWithCalibratedRed:0.68 green:0.02 blue:0.14 alpha:1] setFill];[badge fill];
    [NSColor.whiteColor setStroke];badge.lineWidth=1.5;[badge stroke];
    NSString* text=[NSString stringWithFormat:@"%u",label.number];
    NSDictionary* attrs=@{NSFontAttributeName:[NSFont monospacedDigitSystemFontOfSize:14
        weight:NSFontWeightBold],NSForegroundColorAttributeName:NSColor.whiteColor};
    const NSSize size=[text sizeWithAttributes:attrs];
    [text drawAtPoint:NSMakePoint(NSMidX(label.bounds)-size.width/2,
        NSMidY(label.bounds)-size.height/2) withAttributes:attrs];
  }
}
// Own immutable bytes and values only; this helper cannot touch a store or view.
// The same production decode path is exercised by dependent AppKit tests.
static CGImageRef InspectorDecodePreview(std::shared_ptr<const seethis::core::Bytes> bytes,
    const STPreviewNumbers& model) {
  if(!bytes || bytes->empty() || model.source.width<=0 || model.source.height<=0 ||
     model.geometryVersion!=seethis::core::ReferenceStore::kDisplayUnderlayGeometryVersion ||
     model.digest.empty() || seethis::core::Sha256(*bytes)!=model.digest)return nullptr;
  CGImageRef thumbnail=nullptr;
  NSData* png=[NSData dataWithBytes:bytes->data() length:bytes->size()];
  NSBitmapImageRep* decoded=[[NSBitmapImageRep alloc] initWithData:png];
  CGImageRef source=decoded.CGImage;
  if(source && CGImageGetWidth(source)==model.source.width &&
     CGImageGetHeight(source)==model.source.height) {
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
  return thumbnail;
}

// Cache one immutable raster/model bundle. No number model is attached before
// the same generation's PNG, dimensions and current metadata have been checked.
@interface STInspectorPreviewBundle : NSObject {
 @public STPreviewNumbers model;
}
@property(nonatomic,strong) NSImage* image;
@end
@implementation STInspectorPreviewBundle
@end

static NSString* InspectorSelectedMarkText(const seethis::core::MarkSnapshot& mark) {
  return [NSString stringWithFormat:@"Selected reference\nMark %u\nNote:\n%@",
      mark.number,InspectorMarkNoteText(mark)];
}

// The same persisted projection drives the default endpoint and native labels.
static NSString* TimestampText(std::int64_t utc_us) {
  if(utc_us<=0)return @"Capture time unavailable";
  NSDate* date=[NSDate dateWithTimeIntervalSince1970:utc_us/1000000.0];
  return [NSDateFormatter localizedStringFromDate:date
      dateStyle:NSDateFormatterShortStyle timeStyle:NSDateFormatterMediumStyle];
}
static NSString* InspectorSourceText(const std::optional<std::string>& value) {
  if(!value)return nil;
  return [[NSString alloc] initWithBytes:value->data() length:value->size()
                               encoding:NSUTF8StringEncoding];
}
static NSString* InspectorReferenceName(const seethis::core::SourceContextProjection& source,
                                       bool chrome) {
  if(chrome) {
    NSString* url=InspectorSourceText(source.page_url);
    return url ? [url stringByAppendingString:@" — Chrome"]
               : @"Chrome (captured URL unavailable)";
  }
  NSString* title=InspectorSourceText(source.window_title);
  NSString* app=InspectorSourceText(source.app_name);
  if(title && app && ![title isEqualToString:app])
    return [NSString stringWithFormat:@"%@ — %@",title,app];
  return title ?: app ?: InspectorSourceText(source.bundle_id) ?: @"Reference";
}
static NSString* InspectorReferenceRow(NSString* name,NSString* time) {
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
struct STInspectorReferenceLabel {
  NSString* row;
  NSString* full;
  NSString* time;
};
static STInspectorReferenceLabel InspectorReferenceLabel(
    const seethis::core::ReferenceJobSnapshot& job) {
  const auto source=seethis::core::ProjectSourceContext(job);
  const bool chrome=source.bundle_id && *source.bundle_id=="com.google.Chrome";
  NSString* name=InspectorReferenceName(source,chrome);
  NSString* time=TimestampText(source.image_completed_utc_us.value_or(0));
  NSString* row=chrome && source.page_url
      ? InspectorReferenceRow(InspectorSourceText(source.page_url),
          [@"Chrome — " stringByAppendingString:time])
      : InspectorReferenceRow(name,time);
  return {row,[NSString stringWithFormat:@"%@ — %@",name,time],time};
}

#if !defined(SEETHIS_INSPECTOR_REFERENCE_ACTIONS_TESTING) && \
    !defined(SEETHIS_INSPECTOR_MARK_NOTES_TESTING)

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
- (BOOL)canBecomeKeyWindow {return YES;}
- (BOOL)canBecomeMainWindow {return NO;}
@end

struct STDeleteChord { bool key_down = false; bool exact_modifiers = false; };

@interface STOverlayCoordinator : NSObject <NSWindowDelegate,STOverlayRouting> {
  std::unique_ptr<seethis::core::MarkController> _marks;
  seethis::core::ReferenceStore* _references;
  seethis::core::SettingsStore* _settings;
  seethis::service::ReferenceServer* _server;
  seethis::core::DeleteGestureController* _deletion;
  std::string _hoveredID;
  std::string _activeID;
  std::string _badgeFingerprint;
  bool _badgeCacheValid;
  std::chrono::steady_clock::time_point _badgeCheckedAt;
  std::vector<STNoteBadge> _noteBadges;
  seethis::core::GlobalVisibilityState _presentedGlobalVisibility;
  __strong STMarkNoteEditor* _noteEditor;
  __strong STNoteVisibilityCoordinator* _noteVisibility;
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
  bool _ownSourcePagePending;
  std::uint64_t _ownSourceRequestGeneration;
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
  __strong NSCache<NSString*,STInspectorPreviewBundle*>* _inspectorThumbnailCache;
  __strong NSMutableSet<NSString*>* _inspectorThumbnailPendingKeys;
  __strong NSMutableSet<NSString*>* _inspectorThumbnailFailedKeys;
  dispatch_queue_t _inspectorThumbnailQueue;
  std::string _acknowledgementID;
  std::string _inspectorUnderlayReadyID;
  std::string _inspectorUnderlayReadyDigest;
  std::string _inspectorAnnotatedReadyID;
  std::string _inspectorAnnotatedReadyDigest;
  std::string _inspectorAnnotatedCopyID;
  std::string _selectedMarkID;
  seethis::platform::AppendCaptureMode _appendMode;
  __strong NSButton* _appendModeButton;

  std::string _previewRootID,_previewShotID,_appendSelectedMark;
  std::uint64_t _previewRevision;
  std::uint64_t _appendPresentationGeneration;
  bool _appendCapture;
  seethis::platform::ViewportContinuity _viewportContinuity;
  bool _inspectorAnnotatedCopyInFlight;
  STDeleteAllReferencesAction _deleteAllReferences;
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
- (void)observeViewportInput:(seethis::platform::ViewportInput)event
                   pointer:(const std::optional<seethis::core::DisplayPoint>&)pointer;
- (void)inspectorPreviousShot:(id)sender;
- (void)inspectorNextShot:(id)sender;
- (void)rebuildDisplays;
- (void)updatePointerPolicy:(const std::optional<seethis::core::DisplayPoint>&)pointer;
- (std::optional<seethis::core::DisplayPoint>)displayPointAtGlobalLogical:(seethis::core::Point2D)global;
- (void)paintDisplay:(seethis::core::DisplayId)display;
- (void)refreshNoteBadges;
- (void)invalidateNoteBadges;
- (void)refreshNoteBadgesForVisibility:(const seethis::core::GlobalVisibilitySnapshot&)visibility;
- (void)refreshGlobalVisibilityPresentation;
- (std::vector<seethis::core::Rect>)ownPresentationPanelBounds;
- (std::optional<STNoteBadge>)noteBadgeAtPoint:(seethis::core::DisplayPoint)point;
- (BOOL)handleNoteBadgeClick:(seethis::core::DisplayPoint)point;
- (STDeleteChord)physicalDeleteChord;
- (BOOL)click:(seethis::core::DisplayPoint)point;
- (BOOL)beginRegion:(seethis::core::DisplayPoint)point;
- (void)moveRegion:(seethis::core::DisplayPoint)point;
- (void)endRegion:(seethis::core::DisplayPoint)point;
- (void)abortPending:(NSString*)reason;
- (void)inspectorToggleAppendMode:(id)sender;
- (void)validateAppendMode;
- (BOOL)beginCapturePrefix;
- (void)confirmCapturePrefix;
- (void)cancelCapturePrefix;

- (void)toggleInspector;
- (void)showInspectorIfAllowed:(seethis::core::InspectorSelectionTrigger)trigger;
- (BOOL)inspectorVisible;
- (void)refreshInspector;
- (void)refreshInspectorChromeContext;
- (void)activateInspectorChromeContext:(BOOL)active;
- (void)requestInspectorThumbnail:(std::shared_ptr<const seethis::core::Bytes>)bytes
                        identity:(NSString*)identifier
                          digest:(NSString*)digest
                             key:(NSString*)key
                           model:(STPreviewNumbers)model;
- (void)prepareInspectorSelection:(const std::string&)identifier;
- (void)selectAcceptedInspectorCapture;
- (void)completeInspectorCapture:(seethis::core::CaptureResult)result
                     generation:(std::uint64_t)generation;
- (void)changeInspectorShot:(int)delta;
- (BOOL)recopyInspectorSelection:(const std::string&)identifier;
- (void)inspectorSelectReference:(NSString*)identifier;
- (void)inspectorSelectMark:(NSString*)identifier;
- (void)inspectorOpenMark:(NSString*)identifier screenPoint:(NSPoint)point;
- (void)inspectorToggleMarkVisibility:(id)sender;
- (std::optional<std::string>)visibleHitIdentity:(seethis::core::DisplayPoint)point
                                              radius:(double)radius;
- (BOOL)referenceCanCopy:(const std::string&)identifier;
- (void)inspectorCopyReference:(id)sender;
- (void)inspectorCopyAnnotated:(id)sender;
- (void)inspectorViewDetails:(id)sender;
- (void)inspectorDeleteSelected:(id)sender;
- (void)inspectorDeleteAll:(id)sender;
- (void)showAcknowledgementForID:(const std::string&)identifier;
  - (void)ensureInspectorOnVisibleScreen;
- (void)refreshChromePage;
- (void)stopChromePageRefresh;
#if defined(SEETHIS_WINDOW_OBSERVATION_API)
- (void)refreshForegroundWindow;
- (BOOL)ownsPresentationInput;
- (void)refreshOwnUIPresentationSource;
- (void)publishForegroundUnavailable;
- (void)invalidateVisibilityPresentation;
#endif
@end

@interface STInspectorView : NSView <STInspectorReferenceLayout> {
 @public STPreviewNumbers _previewNumbers;
}
@property(nonatomic,copy) NSString* text;
@property(nonatomic,copy) NSString* selectedReferenceID;
@property(nonatomic,copy) NSArray<NSString*>* markIDs;
@property(nonatomic,copy) NSArray<NSString*>* markRows;
@property(nonatomic,copy) NSString* selectedMarkID;
@property(nonatomic) BOOL globalReferenceVisible;
@property(nonatomic,copy) NSString* globalVisibilityName;
@property(nonatomic) BOOL markVisibilityAvailable;
@property(nonatomic,copy) NSString* thumbnailKey;
@property(nonatomic,copy) NSString* previewState;
@property(nonatomic,strong) NSImage* thumbnail;
@property(nonatomic) NSRect previewFrame;
@property(nonatomic) BOOL referenceCopyAvailable;
@property(nonatomic) BOOL detailsAvailable;
@property(nonatomic) BOOL annotatedCopyAvailable;
@property(nonatomic) BOOL deleteAvailable;
@property(nonatomic) BOOL deleteAllAvailable;
@property(nonatomic) BOOL deleteAllRetry;
@property(nonatomic,weak) STOverlayCoordinator* coordinator;
@property(nonatomic,strong) NSButton* previousShotButton;
@property(nonatomic,strong) NSButton* nextShotButton;
@property(nonatomic,strong) NSTextField* shotLabel;
@property(nonatomic) NSUInteger shotIndex;
@property(nonatomic) NSUInteger shotCount;
@property(nonatomic,strong) NSButton* referenceCopyButton;
@property(nonatomic,strong) NSButton* detailsButton;
@property(nonatomic,strong) NSButton* annotatedCopyButton;
@property(nonatomic,strong) NSButton* deleteButton;
@property(nonatomic,strong) NSButton* deleteAllButton;
@property(nonatomic,strong) NSButton* markVisibilityButton;
- (void)fitToWidth:(CGFloat)width;
@end
@implementation STInspectorView
- (BOOL)isOpaque {return YES;}
- (BOOL)isFlipped {return YES;}
- (instancetype)initWithFrame:(NSRect)frame {
  self=[super initWithFrame:frame];
  if(self) {
    _previousShotButton=[NSButton buttonWithTitle:@"Previous" target:nil action:@selector(inspectorPreviousShot:)];
    _nextShotButton=[NSButton buttonWithTitle:@"Next" target:nil action:@selector(inspectorNextShot:)];
    _shotLabel=[NSTextField labelWithString:@""];
    [self addSubview:_previousShotButton];[self addSubview:_shotLabel];[self addSubview:_nextShotButton];
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
    _deleteAllButton=[NSButton buttonWithTitle:@"Delete All…" target:nil
                                      action:@selector(inspectorDeleteAll:)];
    _markVisibilityButton=[NSButton buttonWithTitle:@"Hide Marks" target:nil
        action:@selector(inspectorToggleMarkVisibility:)];
    _markVisibilityButton.toolTip=@"Show or hide marks for all references.";
    _markVisibilityButton.accessibilityLabel=@"All references: Hide Marks";
    self.toolTip=@"Click a mark to select it. Double-click to edit its note. The append switch controls capture ownership.";
    _deleteAllButton.toolTip=@"Review and delete the listed references from this Mac.";
    _deleteAllButton.accessibilityLabel=@"Delete all references";
    for(NSButton* button in @[_referenceCopyButton,_annotatedCopyButton,
                              _detailsButton,_deleteButton,_markVisibilityButton,_deleteAllButton]) {
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
  self.previousShotButton.target=coordinator;self.nextShotButton.target=coordinator;
  self.referenceCopyButton.target=coordinator;
  self.detailsButton.target=coordinator;
  self.annotatedCopyButton.target=coordinator;
  self.deleteButton.target=coordinator;
  self.deleteAllButton.target=coordinator;
  self.markVisibilityButton.target=coordinator;
}
- (void)setText:(NSString*)text {
  _text=[text copy];
  [self setNeedsDisplay:YES];
}
- (void)setSelectedReferenceID:(NSString*)identifier {
  if(![_selectedReferenceID isEqualToString:identifier]) {
    _thumbnail=nil;
    _thumbnailKey=nil;
    _previewNumbers={};
  }
  _selectedReferenceID=[identifier copy];
  [self setNeedsDisplay:YES];
}
- (void)setThumbnailKey:(NSString*)key {
  if(![_thumbnailKey isEqualToString:key]){_thumbnail=nil;_previewNumbers={};}
  _thumbnailKey=[key copy];
  [self setNeedsDisplay:YES];
}
- (void)fitToWidth:(CGFloat)width {
  NSDictionary* attributes=@{NSFontAttributeName:[NSFont systemFontOfSize:13]};
  const NSRect bounds=[_text boundingRectWithSize:NSMakeSize(MAX(1,width-32),CGFLOAT_MAX)
      options:NSStringDrawingUsesLineFragmentOrigin|NSStringDrawingUsesFontLeading
      attributes:attributes];
  const BOOL selected=self.selectedReferenceID.length>0;
  const BOOL carousel=self.shotCount>1;
  self.previousShotButton.hidden=!carousel;self.nextShotButton.hidden=!carousel;self.shotLabel.hidden=!carousel;
  self.previousShotButton.enabled=carousel&&self.shotIndex>0;
  self.nextShotButton.enabled=carousel&&self.shotIndex+1<self.shotCount;
  self.shotLabel.stringValue=[NSString stringWithFormat:@"Shot %lu of %lu",self.shotIndex+1,self.shotCount];
  self.previousShotButton.frame=NSMakeRect(16,0,90,28);
  self.shotLabel.frame=NSMakeRect(116,5,MAX(1,width-232),20);
  self.shotLabel.alignment=NSTextAlignmentCenter;
  self.nextShotButton.frame=NSMakeRect(MAX(116,width-106),0,90,28);
  self.referenceCopyButton.hidden=!selected;
  self.detailsButton.hidden=!selected;
  self.annotatedCopyButton.hidden=!selected;
  self.deleteButton.hidden=!selected;
  self.markVisibilityButton.hidden=NO;
  self.referenceCopyButton.enabled=selected&&self.referenceCopyAvailable;
  self.detailsButton.enabled=selected&&self.detailsAvailable;
  self.annotatedCopyButton.enabled=selected&&self.annotatedCopyAvailable;
  self.deleteButton.enabled=selected&&self.deleteAvailable;
  self.markVisibilityButton.enabled=self.markVisibilityAvailable;
  self.markVisibilityButton.title=[self.globalVisibilityName isEqualToString:@"uncertain"] ||
      [self.globalVisibilityName isEqualToString:@"unavailable"]
      ? @"Retry visibility save"
      : self.globalReferenceVisible?@"Hide Marks":@"Show Marks";
  self.markVisibilityButton.accessibilityLabel=[@"All references: " stringByAppendingString:self.markVisibilityButton.title];
  self.deleteAllButton.title=self.deleteAllRetry?@"Retry cleanup…":@"Delete All…";
  self.deleteAllButton.enabled=self.deleteAllAvailable;
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
  NSScrollView* scroll=self.enclosingScrollView;
  const CGFloat viewportHeight=scroll?NSHeight(scroll.contentView.bounds):410;
  const auto layout=InspectorBodyLayout(width,viewportHeight,NSHeight(bounds),
      self.markRows,selected);
  self.referenceCopyButton.frame=NSMakeRect(left,layout.controlsTop+4,leftWidth,34);
  self.annotatedCopyButton.frame=NSMakeRect(right,layout.controlsTop+4,rightWidth,34);
  self.detailsButton.frame=NSMakeRect(left,layout.controlsTop+46,leftWidth,34);
  self.deleteButton.frame=NSMakeRect(right,layout.controlsTop+46,rightWidth,34);
  self.previewFrame=layout.previewFrame;
  self.frame=NSMakeRect(0,0,width,layout.documentHeight);
}
- (void)drawRect:(NSRect)dirtyRect {
  (void)dirtyRect;
  [NSColor.windowBackgroundColor setFill];NSRectFill(self.bounds);
  NSDictionary* attributes=@{
    NSFontAttributeName:[NSFont systemFontOfSize:13],
    NSForegroundColorAttributeName:NSColor.labelColor};
  const auto rowLayout=InspectorBodyLayout(NSWidth(self.bounds),0,0,
      self.markRows,NO);
  if(self.markRows.count>0) {
    [@"Marks · double-click to edit note" drawAtPoint:
        NSMakePoint(16,NSMinY(rowLayout.markRowFrames.front())-16)
        withAttributes:attributes];
    for(NSUInteger index=0;index<self.markRows.count;++index) {
      const NSRect rowFrame=rowLayout.markRowFrames[index];
      if([self.markIDs[index] isEqualToString:self.selectedMarkID]) {
        [[NSColor.selectedControlColor colorWithAlphaComponent:0.2] setFill];
        NSRectFill(rowFrame);
      }
      [self.markRows[index] drawInRect:NSMakeRect(24,NSMinY(rowFrame)+6,
          MAX(1,NSWidth(rowFrame)-32),NSHeight(rowFrame)-12)
          withAttributes:attributes];
    }
  }
  NSString* text=self.text ? self.text : @"No reference selected";
  NSDictionary* fontAttributes=@{NSFontAttributeName:[NSFont systemFontOfSize:13]};
  const NSRect textBounds=[text boundingRectWithSize:NSMakeSize(MAX(1,NSWidth(self.bounds)-32),CGFLOAT_MAX)
      options:NSStringDrawingUsesLineFragmentOrigin|NSStringDrawingUsesFontLeading
      attributes:fontAttributes];
  const auto layout=InspectorBodyLayout(NSWidth(self.bounds),NSHeight(self.bounds),
      NSHeight(textBounds),self.markRows,self.selectedReferenceID.length>0);
  [text drawInRect:NSMakeRect(16,layout.textTop,NSWidth(self.bounds)-32,
      ceil(NSHeight(textBounds)))
      withAttributes:attributes];
  if(!NSIsEmptyRect(self.previewFrame)) {
    [[NSColor.separatorColor colorWithAlphaComponent:0.7] setStroke];
    [[NSBezierPath bezierPathWithRoundedRect:self.previewFrame xRadius:8 yRadius:8] stroke];
    const NSRect imageFrame=self.thumbnail?
        InspectorAspectFit(self.thumbnail.size,self.previewFrame):NSZeroRect;
    if(!NSIsEmptyRect(imageFrame)) {
      InspectorDrawPreview(self.thumbnail,self.previewFrame,_previewNumbers,self.thumbnailKey);
    } else {
      NSString* state=self.previewState.length?self.previewState:@"Preview unavailable.";
      const NSSize size=[state sizeWithAttributes:attributes];
      [state drawAtPoint:NSMakePoint(NSMidX(self.previewFrame)-size.width/2,
                                   NSMidY(self.previewFrame)-size.height/2)
          withAttributes:attributes];
    }
  }
}
- (void)mouseDown:(NSEvent*)event {
  NSPoint point=[self convertPoint:event.locationInWindow fromView:nil];
  const auto layout=InspectorBodyLayout(NSWidth(self.bounds),0,0,self.markRows,NO);
  const NSInteger markRow=InspectorMarkRowAtPoint(point,layout);
  if(markRow!=NSNotFound) {
    NSString* identifier=self.markIDs[static_cast<NSUInteger>(markRow)];
    [self.coordinator inspectorSelectMark:identifier];
    if(InspectorMarkAction(event.clickCount)==STInspectorMarkAction::kOpenNote)
      [self.coordinator inspectorOpenMark:identifier screenPoint:[self.window convertPointToScreen:event.locationInWindow]];
  }
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
    __weak STOverlayCoordinator* weakAppendSelf=self;
    _marks->SetAppendCompletion([weakAppendSelf](seethis::core::AppendResult result,std::uint64_t generation) {
      STOverlayCoordinator* owner=weakAppendSelf;if(!owner)return;
      const bool success=result.status==seethis::core::AppendStatus::kCommitted||
          result.status==seethis::core::AppendStatus::kIdempotent;
      if(success&&generation==owner->_generation) {
        if(owner->_inspectorLifecycle.selected_id==result.reference_id &&
           owner->_inspectorLifecycle.selection_generation==owner->_appendPresentationGeneration) {
          owner->_previewRootID=result.reference_id;owner->_previewShotID=result.shot_id;
          owner->_previewRevision=result.revision;
          ++owner->_inspectorLifecycle.selection_generation;
        }
      }
      if(generation==owner->_generation) {
        owner->_appendCapture=false;owner->_inspectorCaptureSelectionPending=false;
        owner->_inspectorActionStatus=success?@"Screenshot added.":@"Screenshot was not confirmed. Saved reference retained.";
      }
      [owner invalidateVisibilityPresentation];[owner refreshInspector];
    });
    _noteEditor=[[STMarkNoteEditor alloc]
        initWithRead:[references](std::string_view referenceID,
                                 std::string_view markID)->STNoteReadResult {
          const auto snapshot=references->MarkSnapshots(referenceID);
          STNoteReadResult result;
          result.revision=snapshot.revision;
          if(snapshot.status!=seethis::core::MarkNotesStatus::kReady &&
             snapshot.status!=seethis::core::MarkNotesStatus::kPending)return result;
          const auto found=std::find_if(snapshot.marks.begin(),snapshot.marks.end(),
              [markID](const auto& mark) {return mark.id==markID;});
          if(found==snapshot.marks.end())return result;
          result.status=snapshot.status==seethis::core::MarkNotesStatus::kPending?
              STNoteReadStatus::kPending:STNoteReadStatus::kReady;
          result.note=found->note;
          return result;
        } save:[references](std::string_view referenceID,std::string_view markID,
                          std::string_view note,std::uint64_t revision)
                          ->STNoteSaveResult {
          const auto saved=references->UpdateMarkNote(
              referenceID,markID,std::string(note),revision);
          STNoteSaveResult result;result.revision=saved.revision;
          switch(saved.status) {
            case seethis::core::MarkNoteUpdateStatus::kUpdated:
              result.status=STNoteSaveStatus::kUpdated;break;
            case seethis::core::MarkNoteUpdateStatus::kRevisionConflict:
              result.status=STNoteSaveStatus::kConflict;break;
            case seethis::core::MarkNoteUpdateStatus::kNotFound:
            case seethis::core::MarkNoteUpdateStatus::kDeleted:
            case seethis::core::MarkNoteUpdateStatus::kExpired:
              result.status=STNoteSaveStatus::kUnavailable;break;
            case seethis::core::MarkNoteUpdateStatus::kCommitUncertain:
              result.status=STNoteSaveStatus::kCommitUncertain;break;
            case seethis::core::MarkNoteUpdateStatus::kInvalid:
              result.status=references->MarkSnapshots(referenceID).status==
                  seethis::core::MarkNotesStatus::kPending?
                  STNoteSaveStatus::kUnavailable:STNoteSaveStatus::kFailed;
              break;
            case seethis::core::MarkNoteUpdateStatus::kCorrupt:
            case seethis::core::MarkNoteUpdateStatus::kUnsupported:
            case seethis::core::MarkNoteUpdateStatus::kIOError:
              result.status=STNoteSaveStatus::kFailed;break;
          }
          return result;
        } copy:[server](std::string_view referenceID)->STNoteCopyStatus {
          if(!server)return STNoteCopyStatus::kUnavailable;
          const auto text=server->ClipboardText(referenceID);
          if(text.empty())return STNoteCopyStatus::kUnavailable;
          return seethis::platform::CopyReferenceText(text)?
              STNoteCopyStatus::kCopied:STNoteCopyStatus::kFailed;
        }];
    __weak STOverlayCoordinator* weakVisibilitySelf=self;
    STNoteVisibilitySource visibilitySource;
    visibilitySource.marks=_marks.get();
    visibilitySource.visibility=[settings] {return settings->Visibility();};
    visibilitySource.lookup=[references](std::string_view id){return references->LookupMetadata(id);};
    visibilitySource.snapshots=[references](std::string_view id){return references->MarkSnapshots(id);};
    visibilitySource.collection=[references](std::string_view id){return references->Collection(id);};
    visibilitySource.displays=[weakVisibilitySelf] {
      STOverlayCoordinator* owner=weakVisibilitySelf;
      return owner?owner->_displays:std::vector<seethis::core::FrozenDisplay>{};
    };
    visibilitySource.foregroundPID=[] {return static_cast<std::int64_t>(
        NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier);};
    visibilitySource.screenReady=[] {return seethis::platform::ScreenRecordingReady();};
    _noteVisibility=[[STNoteVisibilityCoordinator alloc] initWithEditor:_noteEditor
                                                               source:std::move(visibilitySource)];
    _inspectorPanel=[[STInspectorPanel alloc]
        initWithContentRect:NSMakeRect(120,120,470,430)
        styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|
          NSWindowStyleMaskNonactivatingPanel
        backing:NSBackingStoreBuffered defer:NO];
    _inspectorPanel.title=@"SeeThis Inspector";
    _inspectorPanel.level=STOwnPresentationPanelLevel();
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
    _appendModeButton=[NSButton checkboxWithTitle:@"Append screenshots: OFF" target:self
        action:@selector(inspectorToggleAppendMode:)];
    _appendModeButton.font=[NSFont systemFontOfSize:11];
    _appendModeButton.cell.lineBreakMode=NSLineBreakByTruncatingTail;
    _appendModeButton.toolTip=@"ON adds each shortcut capture to the bound reference. OFF creates a new reference.";
    inspectorContainer.appendModeButton=_appendModeButton;
    [inspectorContainer addSubview:_appendModeButton];
    inspectorContainer.markVisibilityButton=_inspectorView.markVisibilityButton;
    inspectorContainer.deleteAllButton=_inspectorView.deleteAllButton;
    [inspectorContainer addSubview:inspectorContainer.markVisibilityButton];
    [inspectorContainer addSubview:inspectorContainer.deleteAllButton];
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
        if(owner) {
          [owner refreshGlobalVisibilityPresentation];
          if(owner->_inspectorPanel.isVisible)[owner refreshInspector];
        }
      }];
    _marks->SetPresentationPermitted(_settings->Visibility().permits_presentation());
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
      object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification* notification) {
        STOverlayCoordinator* owner=weakSelf;
        if(!owner)return;
#if defined(SEETHIS_WINDOW_OBSERVATION_API)
        NSRunningApplication* activated=notification.userInfo[NSWorkspaceApplicationKey];
        [owner->_noteVisibility applicationActivated:activated.processIdentifier];
        if([owner ownsPresentationInput])STBeginOwnUIPresentation(*owner->_marks);
        else if(activated.processIdentifier!=NSProcessInfo.processInfo.processIdentifier) {
          // A real external return is observed before the private channel ends.
          [owner refreshForegroundWindow];return;
        } else owner->_marks->RetireLiveShot();
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
  [_noteEditor hide];
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
                             key:(NSString*)key
                           model:(STPreviewNumbers)model {
  if(!bytes || bytes->empty() ||
     model.digest!=(digest.UTF8String?:"") ||
     seethis::core::Sha256(*bytes)!=model.digest ||
     [_inspectorThumbnailPendingKeys containsObject:key])return;
  [_inspectorThumbnailPendingKeys addObject:key];
  const auto presentationGeneration=_inspectorLifecycle.selection_generation;
  __weak STOverlayCoordinator* weakOwner=self;
  dispatch_async(_inspectorThumbnailQueue,^{
    CGImageRef thumbnail=nullptr;
    @autoreleasepool {
      thumbnail=InspectorDecodePreview(bytes,model);
    }
    dispatch_async(dispatch_get_main_queue(),^{
      STOverlayCoordinator* owner=weakOwner;
      if(owner) {
        [owner->_inspectorThumbnailPendingKeys removeObject:key];
        const std::string selected=identifier.UTF8String;
        const auto current=owner->_references->LookupMetadata(selected);
        const auto collection=owner->_references->Collection(selected);
        const auto shot=std::find_if(collection.shots.begin(),collection.shots.end(),[&](const auto& value){return value.id==model.shotID;});
        const auto metadata=shot==collection.shots.end()?std::shared_ptr<const seethis::core::Reference>{}:shot->metadata;
        const auto asset=metadata?owner->_references->RequestShotAnnotated(selected,model.shotID,false):seethis::core::ReadyAssetResult{};
        const BOOL ready=current && metadata && collection.revision==model.contentRevision &&
            owner->_previewShotID==model.shotID &&
            current->state==seethis::core::ReferenceJobState::kReady &&
            metadata->source.sha256==model.sourceSHA &&
            metadata->source.width==model.source.width && metadata->source.height==model.source.height &&
            seethis::core::Sha256(seethis::core::Serialize(*metadata))==model.metadataSHA &&
            asset.state==seethis::core::ReadyAssetState::kReady && asset.byte_sha256==model.digest;
        const BOOL matches=InspectorPreviewCompletionMatches(
            seethis::platform::InspectorPresentationIsCurrent(owner->_inspectorLifecycle,
                selected,presentationGeneration),ready,selected,
            owner->_inspectorUnderlayReadyID,owner->_inspectorUnderlayReadyDigest,
            digest,owner->_inspectorView.thumbnailKey,key);
        NSImage* image=nil;
        if(thumbnail && matches)
          image=[[NSImage alloc] initWithCGImage:thumbnail size:model.source];
        if(image) {
          STInspectorPreviewBundle* bundle=[STInspectorPreviewBundle new];
          bundle.image=image;bundle->model=model;
          const NSUInteger cost=CGImageGetWidth(thumbnail)*CGImageGetHeight(thumbnail)*4;
          // Currentness is required before cache publication as well as attachment.
          [owner->_inspectorThumbnailCache setObject:bundle forKey:key cost:cost];
          owner->_inspectorView->_previewNumbers=bundle->model;
          owner->_inspectorView.thumbnail=bundle.image;
          owner->_inspectorView.previewState=nil;
          [owner->_inspectorView setNeedsDisplay:YES];
        } else if(ready && seethis::platform::InspectorPresentationIsCurrent(
                     owner->_inspectorLifecycle,selected,presentationGeneration)) {
          if(owner->_inspectorThumbnailFailedKeys.count>=32)
            [owner->_inspectorThumbnailFailedKeys removeAllObjects];
          [owner->_inspectorThumbnailFailedKeys addObject:key];
          if([owner->_inspectorView.thumbnailKey isEqualToString:key]) {
            owner->_inspectorView.thumbnail=nil;
            owner->_inspectorView->_previewNumbers={};
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
- (void)validateAppendMode {
  if(const auto previous=STValidateAppendMode(_appendMode,*_references,*_marks)) {
    if(_appendCapture&&_activeID==*previous)[self abortPending:@"Append reference unavailable. Append mode is OFF."];
    _inspectorActionStatus=@"Append mode is OFF: bound reference unavailable.";
  }
}
- (void)refreshInspector {
  [self validateAppendMode];
  [_noteVisibility reconcile];
  if(!_inspectorView)return;
  [self refreshInspectorChromeContext];
  NSMutableArray<NSString*>* ids=[NSMutableArray array];
  NSMutableArray<NSString*>* rows=[NSMutableArray array];
  NSMutableArray<NSString*>* times=[NSMutableArray array];
  NSMutableArray<NSString*>* fullRows=[NSMutableArray array];
  for(const auto& item:_marks->inspector_jobs()) {
    [ids addObject:[NSString stringWithUTF8String:item.id.c_str()]];
    const auto label=InspectorReferenceLabel(item);
    [times addObject:label.time];
    [rows addObject:label.row];
    [fullRows addObject:label.full];
  }
  _inspectorView.deleteAllAvailable=!_deleteAllReferences.dialogOpen() &&
      (ids.count>0 || _deleteAllReferences.hasPendingReview());
  _inspectorView.deleteAllRetry=_deleteAllReferences.hasPendingReview();
  NSString* selectedID=[NSString stringWithUTF8String:
      _inspectorLifecycle.selected_id.c_str()];
  _inspectorView.selectedReferenceID=selectedID;
  _inspectorReferenceSelector.referenceIDs=ids;
  _inspectorReferenceSelector.referenceRows=rows;
  _inspectorReferenceSelector.referenceFullRows=fullRows;
  _inspectorReferenceSelector.referenceTimes=times;
  _inspectorReferenceSelector.selectedReferenceID=selectedID;
  [_inspectorReferenceSelector refreshMenu];
  const auto selected_job=_inspectorLifecycle.selected_id.empty()
      ? std::optional<seethis::core::ReferenceJobSnapshot>{}
      : _references->LookupMetadata(_inspectorLifecycle.selected_id);
  const auto collection=_references->Collection(_inspectorLifecycle.selected_id);
  _appendModeButton.state=_appendMode.enabled()?NSControlStateValueOn:NSControlStateValueOff;
  _appendModeButton.enabled=_appendMode.enabled()||collection.status==seethis::core::MarkNotesStatus::kReady;
  NSString* bound=@"";
  if(_appendMode.enabled()) {
    const auto owner=_references->LookupMetadata(_appendMode.root);
    if(owner)bound=InspectorReferenceLabel(*owner).row;
  }
  _appendModeButton.title=_appendMode.enabled()?[NSString stringWithFormat:@"Append screenshots: ON — %@",bound]:@"Append screenshots: OFF";
  _appendModeButton.accessibilityLabel=_appendModeButton.title;
  if(_previewRootID!=_inspectorLifecycle.selected_id) {
    _previewRootID=_inspectorLifecycle.selected_id;_previewShotID.clear();_previewRevision=0;
  }
  if(collection.status==seethis::core::MarkNotesStatus::kReady && !collection.shots.empty()) {
    if(std::none_of(collection.shots.begin(),collection.shots.end(),[&](const auto& shot){return shot.id==_previewShotID;}))
      _previewShotID=collection.shots.front().id;
    _previewRevision=collection.revision;
  }
  const auto active=std::find_if(collection.shots.begin(),collection.shots.end(),[&](const auto& shot){return shot.id==_previewShotID;});
  _inspectorView.shotCount=collection.shots.size();
  _inspectorView.shotIndex=active==collection.shots.end()?0:std::distance(collection.shots.begin(),active);
  const auto shotMarks=seethis::platform::MarksForShot(_references->MarkSnapshots(_inspectorLifecycle.selected_id),_previewShotID);
  NSMutableArray<NSString*>* markIDs=[NSMutableArray array];
  NSMutableArray<NSString*>* markRows=[NSMutableArray array];
  NSString* selectedMarkText=nil;
  bool selectedMarkAvailable=false;
  if(selected_job && selected_job->state==seethis::core::ReferenceJobState::kReady) {
    const auto& snapshot=shotMarks;
    if(snapshot.status==seethis::core::MarkNotesStatus::kReady) {
      for(const auto& mark:snapshot.marks) {
        [markIDs addObject:[NSString stringWithUTF8String:mark.id.c_str()]];
        [markRows addObject:InspectorMarkRowText(mark)];
        if(mark.id==_selectedMarkID) {
          selectedMarkAvailable=true;
          selectedMarkText=InspectorSelectedMarkText(mark);
        }
      }
    }
  }
  if(!selectedMarkAvailable)_selectedMarkID.clear();
  _inspectorView.markIDs=markIDs;
  _inspectorView.markRows=markRows;
  _inspectorView.selectedMarkID=[NSString stringWithUTF8String:_selectedMarkID.c_str()];
  const auto visibility=_settings->Visibility();
  _inspectorView.globalReferenceVisible=visibility.permits_presentation();
  _inspectorView.globalVisibilityName=[NSString stringWithUTF8String:visibility.name()];
  _inspectorView.markVisibilityAvailable=_settings->usable();
  if(!visibility.permits_presentation())[_noteVisibility reconcile];
  _inspectorView.referenceCopyAvailable=
      [self referenceCanCopy:_inspectorLifecycle.selected_id];
  _inspectorView.detailsAvailable=selected_job &&
      (selected_job->state==seethis::core::ReferenceJobState::kReady ||
       selected_job->state==seethis::core::ReferenceJobState::kExpired) &&
      _server->port()!=0;
  _inspectorView.detailsButton.toolTip=selected_job?
      [NSString stringWithFormat:@"%s\nSame native window and title can collide; provider-silent truncation detection is unknown.",
          seethis::core::TitleScopeCapabilityName(selected_job->context)]:nil;
  _inspectorView.deleteAvailable=selected_job &&
      selected_job->state!=seethis::core::ReferenceJobState::kDeleted;
  _inspectorView.annotatedCopyAvailable=NO;
  _inspectorAnnotatedReadyID.clear();
  _inspectorAnnotatedReadyDigest.clear();
  _inspectorUnderlayReadyID.clear();_inspectorUnderlayReadyDigest.clear();
  NSString* thumbnailKey=[selectedID stringByAppendingString:@":unavailable"];
  NSString* previewState=@"Preview unavailable.";
  STInspectorPreviewBundle* cached=nil;
  std::shared_ptr<const seethis::core::Bytes> thumbnailBytes;
  NSString* thumbnailDigest=nil;
  STPreviewNumbers model;
  if(selected_job && selected_job->state==seethis::core::ReferenceJobState::kReady &&
     selected_job->metadata) {
    const auto annotated=_references->RequestShotAnnotated(_inspectorLifecycle.selected_id,_previewShotID);
    if(annotated.state==seethis::core::ReadyAssetState::kReady && annotated.bytes &&
       !annotated.bytes->empty() && !annotated.byte_sha256.empty() &&
       seethis::core::Sha256(*annotated.bytes)==annotated.byte_sha256) {
      _inspectorAnnotatedReadyID=_inspectorLifecycle.selected_id;
      _inspectorAnnotatedReadyDigest=annotated.byte_sha256;
      _inspectorView.annotatedCopyAvailable=!_inspectorAnnotatedCopyInFlight;
    }
    const auto asset=_references->RequestShotAnnotated(_inspectorLifecycle.selected_id,_previewShotID,false);
    const auto after=_references->LookupMetadata(_inspectorLifecycle.selected_id);
    if(asset.state==seethis::core::ReadyAssetState::kReady && asset.bytes &&
       !asset.bytes->empty() && !asset.byte_sha256.empty() &&
       seethis::core::Sha256(*asset.bytes)==asset.byte_sha256 && after &&
       after->state==seethis::core::ReferenceJobState::kReady &&
       after->metadata==selected_job->metadata) {
      if(active!=collection.shots.end())model=InspectorPreviewNumbers(*active->metadata,shotMarks,"");
      model.shotID=_previewShotID;model.contentRevision=collection.revision;
      if(model.source.width>0) {
        model.digest=asset.byte_sha256;
        model.key=model.referenceID+":"+model.sourceSHA+":"+model.metadataSHA+":"+
            model.geometryVersion+":"+model.shotID+":"+std::to_string(model.contentRevision)+":"+model.digest;
        thumbnailKey=[NSString stringWithUTF8String:model.key.c_str()];
        thumbnailDigest=[NSString stringWithUTF8String:model.digest.c_str()];
        _inspectorUnderlayReadyID=model.referenceID;
        _inspectorUnderlayReadyDigest=model.digest;
        cached=[_inspectorThumbnailCache objectForKey:thumbnailKey];
        if(cached)previewState=nil;
        else if([_inspectorThumbnailFailedKeys containsObject:thumbnailKey])
          previewState=@"Preview unavailable.";
        else {previewState=@"Preparing preview…";thumbnailBytes=asset.bytes;}
      }
    } else if(asset.state==seethis::core::ReadyAssetState::kPending) {
      thumbnailKey=[selectedID stringByAppendingString:@":pending"];
      previewState=@"Preparing image…";
    }
  } else if(selected_job && (selected_job->state==seethis::core::ReferenceJobState::kPending ||
                            selected_job->state==seethis::core::ReferenceJobState::kIndexing)) {
    thumbnailKey=[selectedID stringByAppendingString:@":pending"];
    previewState=@"Preparing reference…";
  }
  _inspectorView.thumbnailKey=thumbnailKey;
  _inspectorView.thumbnail=cached.image;
  _inspectorView->_previewNumbers=cached?cached->model:STPreviewNumbers{};
  _inspectorView.previewState=previewState;
  if(thumbnailBytes)
    [self requestInspectorThumbnail:thumbnailBytes identity:selectedID
        digest:thumbnailDigest key:thumbnailKey model:model];
  if(_inspectorLifecycle.selected_id.empty()) {
    _inspectorView.text=@"Select a reference above to preview it.";
    [self layoutInspectorText];return;
  }
  const auto& job=selected_job;
  if(!job) {
    _inspectorView.text=@"Reference unavailable.";
    [self layoutInspectorText];return;
  }
  const auto source=active==collection.shots.end()?seethis::core::SourceContextProjection{}:
      seethis::core::ProjectSourceContext(*active->metadata);
  const auto contextText=source.page_url?*source.page_url:source.window_title?*source.window_title:std::string{};
  _inspectorView.text=selectedMarkText?:[NSString stringWithUTF8String:contextText.c_str()];
  [self layoutInspectorText];
}
- (void)layoutInspectorText {
  if(!_inspectorView||!_inspectorScrollView)return;
  if(_inspectorActionStatus.length)
    _inspectorView.text=[_inspectorView.text stringByAppendingFormat:
        @"\n\n%@",_inspectorActionStatus];
  const auto visibility=_settings->Visibility();
  if(visibility.state==seethis::core::GlobalVisibilityState::kUncertain)
    _inspectorView.text=[_inspectorView.text stringByAppendingString:@"\n\nVisibility save uncertain. Marks hidden; retry."];
  else if(visibility.state==seethis::core::GlobalVisibilityState::kUnavailable)
    _inspectorView.text=[_inspectorView.text stringByAppendingString:@"\n\nVisibility unavailable. Marks hidden."];
  else if(visibility.initialized_this_session)
    _inspectorView.text=[_inspectorView.text stringByAppendingString:@"\n\nMarks now share one visibility setting across all references."];
  [_inspectorView fitToWidth:NSWidth(_inspectorScrollView.contentView.bounds)];
  [_inspectorScrollView.superview setNeedsLayout:YES];
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
  if(STCompleteCaptureAttempt(*_marks,_generation,generation,std::move(result),
      _inspectorCaptureSucceeded,_inspectorCaptureSelectionPending)) {
    [self abortPending:@"Capture failed. Saved references retained."];return;
  }
  if(generation==_generation)[self selectAcceptedInspectorCapture];
}
- (void)selectAcceptedInspectorCapture {
  if(_appendCapture)return;
  if(!_inspectorCaptureSelectionPending || !_inspectorCaptureSucceeded || _activeID.empty())return;
  if(_inspectorLifecycle.selection_generation!=_appendPresentationGeneration) {
    _inspectorCaptureSelectionPending=false;return;
  }
  const auto next=seethis::platform::InspectorAfterCapture(
      _inspectorLifecycle,_activeID,_marks->inspector_jobs());
  if(next.selection_generation==_inspectorLifecycle.selection_generation)return;
  _inspectorLifecycle=next;
  _inspectorCaptureSelectionPending=false;
  _selectedMarkID.clear();
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
- (void)changeInspectorShot:(int)delta {
  const auto collection=_references->Collection(_inspectorLifecycle.selected_id);
  const auto active=std::find_if(collection.shots.begin(),collection.shots.end(),[&](const auto& shot){return shot.id==_previewShotID;});
  if(active==collection.shots.end())return;
  const auto next=std::distance(collection.shots.begin(),active)+delta;
  if(next<0||next>=static_cast<std::ptrdiff_t>(collection.shots.size()))return;
  _previewShotID=collection.shots[next].id;
  const auto marks=seethis::platform::MarksForShot(_references->MarkSnapshots(_inspectorLifecycle.selected_id),_previewShotID);
  if(std::none_of(marks.marks.begin(),marks.marks.end(),[&](const auto& mark){return mark.id==_selectedMarkID;}))_selectedMarkID.clear();
  ++_inspectorLifecycle.selection_generation;
  [self refreshInspector];
}
- (void)inspectorPreviousShot:(id)sender {(void)sender;[self changeInspectorShot:-1];}
- (void)inspectorNextShot:(id)sender {(void)sender;[self changeInspectorShot:1];}
- (BOOL)recopyInspectorSelection:(const std::string&)identifier {
  BOOL copied=NO;
  try {copied=_marks->RecopyJob(identifier);}
  catch(...) {}
  _inspectorActionStatus=copied?@"JSON URL copied.":@"JSON URL was not copied.";
  if(copied)[self showAcknowledgementForID:identifier];
  return copied;
}
- (void)inspectorToggleAppendMode:(id)sender {
  (void)sender;
  STToggleInspectorAppendMode(_appendMode,*_references,_inspectorLifecycle,[&](NSString* status) {
    _inspectorActionStatus=status;[self refreshInspector];
  });
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
  _appendMode.ReferenceSelected(identifier.UTF8String);
  if(previousID!=identifier.UTF8String)_selectedMarkID.clear();
  _inspectorLifecycle=seethis::platform::ReduceInspectorLifecycle(
      _inspectorLifecycle,
      seethis::platform::InspectorLifecycleEvent::kReferenceSelection,
      identifier.UTF8String);
  [self prepareInspectorSelection:_inspectorLifecycle.selected_id];
  if(seethis::platform::InspectorSelectionChangesPresentation(
         previousID,_inspectorLifecycle.selected_id))
    for(STOverlayPanel* panel in self.panels)[panel.contentView setNeedsDisplay:YES];
  // Selection may inspect another reference while the note keeps its owner.
  // Only explicit Copy actions may publish during that open editing session.
  if(!_noteEditor.presentationOpen)
    [self recopyInspectorSelection:_inspectorLifecycle.selected_id];
  [self refreshInspector];
  [self updatePointerPolicy:_lastPointer];
}
- (void)inspectorSelectMark:(NSString*)identifier {
  const auto snapshot=_references->MarkSnapshots(_inspectorLifecycle.selected_id);
  if(snapshot.status!=seethis::core::MarkNotesStatus::kReady)return;
  const auto found=std::find_if(snapshot.marks.begin(),snapshot.marks.end(),
      [identifier](const auto& mark) {return mark.id==identifier.UTF8String;});
  if(found==snapshot.marks.end()||found->shot_id!=_previewShotID)return;
  _selectedMarkID=found->id;
  ++_inspectorLifecycle.selection_generation;
  _inspectorActionStatus=nil;
  [self refreshInspector];
}
- (void)inspectorOpenMark:(NSString*)identifier screenPoint:(NSPoint)point {
  const auto snapshot=_references->MarkSnapshots(_inspectorLifecycle.selected_id);
  if(snapshot.status!=seethis::core::MarkNotesStatus::kReady)return;
  const auto found=std::find_if(snapshot.marks.begin(),snapshot.marks.end(),[&](const auto& mark) {
    return mark.id==(identifier.UTF8String?:"")&&mark.shot_id==_previewShotID;
  });
  if(found==snapshot.marks.end())return;
  const BOOL opened=[_noteVisibility openInspectorReference:_inspectorLifecycle.selected_id
      mark:found->id number:found->number screenPoint:point];
  _inspectorActionStatus=opened?nil:@"Note unavailable. Any unsaved draft remains with its owner.";
  [self refreshInspector];
}
- (void)inspectorToggleMarkVisibility:(id)sender {
  (void)sender;
  std::string error;
  const bool saved=_settings->ToggleGlobalVisibility(&error);
  const auto visibility=_settings->Visibility();
  _inspectorActionStatus=saved
      ? (visibility.permits_presentation()?@"All references Visible. Saved.":@"All references Invisible. Saved.")
      : visibility.state==seethis::core::GlobalVisibilityState::kUncertain
          ? @"Visibility save uncertain. Marks hidden; retry."
          : @"Visibility could not be saved. Retry.";
  [_noteVisibility reconcile];
  [self invalidateNoteBadges];
  for(STOverlayPanel* panel in self.panels)[panel.contentView setNeedsDisplay:YES];
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
  const auto asset=_references->RequestShotAnnotated(selected,_previewShotID);
  const auto after=_references->LookupMetadata(selected);
  if(asset.state!=seethis::core::ReadyAssetState::kReady ||
     !asset.bytes || asset.bytes->empty() ||
     asset.byte_sha256.empty() ||
     asset.byte_sha256!=_inspectorAnnotatedReadyDigest ||
     seethis::core::Sha256(*asset.bytes)!=asset.byte_sha256 ||
     !after || before->metadata!=after->metadata || after->state!=seethis::core::ReferenceJobState::kReady ||
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
  _selectedMarkID.clear();
  _inspectorLifecycle=seethis::platform::InspectorAfterSuccessfulDelete(
      _inspectorLifecycle,result,_marks->inspector_jobs());
  [self prepareInspectorSelection:_inspectorLifecycle.selected_id];
  for(STOverlayPanel* panel in self.panels)[panel.contentView setNeedsDisplay:YES];
  _inspectorActionStatus=[NSString stringWithUTF8String:_marks->status().c_str()];
  [self refreshInspector];
  [self updatePointerPolicy:_lastPointer];
}
- (void)inspectorDeleteAll:(id)sender {
  (void)sender;
  const auto review=_deleteAllReferences.Begin(_marks->inspector_jobs());
  if(!review)return;
  // The alert displays the exact ordered identifiers that confirmation uses.
  NSMutableArray<NSString*>* ids=[NSMutableArray arrayWithCapacity:review->ids.size()];
  for(const auto& id:review->ids)
    [ids addObject:[NSString stringWithUTF8String:id.c_str()]];
  NSAlert* alert=[NSAlert new];
  alert.alertStyle=NSAlertStyleWarning;
  alert.messageText=[NSString stringWithFormat:@"Delete %lu reviewed references?",
      static_cast<unsigned long>(ids.count)];
  alert.informativeText=@"Only the references listed below will be deleted from this Mac. References added after this dialog opens will remain.";
  [alert addButtonWithTitle:@"Delete All"];
  [alert addButtonWithTitle:@"Cancel"];
  NSScrollView* list=[[NSScrollView alloc] initWithFrame:NSMakeRect(0,0,430,180)];
  list.hasVerticalScroller=YES;
  list.borderType=NSBezelBorder;
  NSTextView* text=[[NSTextView alloc] initWithFrame:NSMakeRect(0,0,430,180)];
  text.editable=NO;
  text.selectable=YES;
  text.verticallyResizable=YES;
  text.horizontallyResizable=NO;
  text.textContainer.containerSize=NSMakeSize(430,CGFLOAT_MAX);
  text.textContainer.widthTracksTextView=YES;
  text.string=[ids componentsJoinedByString:@"\n"];
  list.documentView=text;
  alert.accessoryView=list;
  const std::uint64_t generation=review->generation;
  __weak STOverlayCoordinator* weakOwner=self;
  [alert beginSheetModalForWindow:_inspectorPanel completionHandler:^(NSModalResponse answer) {
    STOverlayCoordinator* owner=weakOwner;
    if(!owner)return;
    if(answer!=NSAlertFirstButtonReturn) {
      if(owner->_deleteAllReferences.Cancel(generation))[owner refreshInspector];
      return;
    }
    const auto result=owner->_deleteAllReferences.Confirm(generation,
        [owner](std::string_view id) {
          return owner->_marks->DeleteHistoryJob(id);
        });
    if(!result)return;  // A superseded sheet cannot delete anything.
    const auto reviewed=result->deleted+result->already_deleted+
        result->unavailable+result->cleanup_failed;
    if(!owner->_inspectorLifecycle.selected_id.empty()) {
      const auto selected=owner->_references->LookupMetadata(
          owner->_inspectorLifecycle.selected_id);
      if(!selected || selected->state==seethis::core::ReferenceJobState::kDeleted) {
        owner->_selectedMarkID.clear();
        owner->_inspectorLifecycle=seethis::platform::ReduceInspectorLifecycle(
            owner->_inspectorLifecycle,
            seethis::platform::InspectorLifecycleEvent::kSelectedDelete);
      }
    }
    [owner->_inspectorThumbnailCache removeAllObjects];
    [owner->_inspectorThumbnailPendingKeys removeAllObjects];
    [owner->_inspectorThumbnailFailedKeys removeAllObjects];
    owner->_inspectorUnderlayReadyID.clear();
    owner->_inspectorUnderlayReadyDigest.clear();
    owner->_inspectorAnnotatedReadyID.clear();
    owner->_inspectorAnnotatedReadyDigest.clear();
    owner->_inspectorAnnotatedCopyID.clear();
    owner->_inspectorAnnotatedCopyInFlight=false;
    for(STOverlayPanel* panel in owner.panels)
      [panel.contentView setNeedsDisplay:YES];
    if(result->cleanup_failed)
      owner->_inspectorActionStatus=[NSString stringWithFormat:
          @"Deleted or already removed %lu of %lu reviewed references; cleanup failed for %lu. Retry cleanup for the same reviewed set.",
          static_cast<unsigned long>(result->deleted+result->already_deleted),
          static_cast<unsigned long>(reviewed),
          static_cast<unsigned long>(result->cleanup_failed)];
    else
      owner->_inspectorActionStatus=[NSString stringWithFormat:
          @"Deleted or already removed %lu of %lu reviewed references%@.",
          static_cast<unsigned long>(result->deleted+result->already_deleted),
          static_cast<unsigned long>(reviewed),
          result->unavailable?@"; some were already unavailable":@""];
    [owner refreshInspector];
    [owner updatePointerPolicy:owner->_lastPointer];
  }];
  [self refreshInspector];
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
  _inspectorCaptureSelectionPending=false;_inspectorCaptureSucceeded=false;_appendCapture=false;
  seethis::platform::CancelCaptureAttempt(*self.controller,*_marks,reason.UTF8String?:"Capture cancelled");
  _previousState=self.controller->Snapshot().state;
#if defined(SEETHIS_WINDOW_OBSERVATION_API)
  _captureContext.reset();
#endif
  _inspectorActionStatus=reason;
  for(STOverlayPanel* panel in self.panels){panel.ignoresMouseEvents=YES;[panel.contentView setNeedsDisplay:YES];}
}
- (void)rebuildDisplays {
  _deletion->Interrupt(_deletion->Snapshot().key_down);_hoveredID.clear();
  [self invalidateNoteBadges];
  _lastPointer.reset();_pendingHoverPresentationDisplays.clear();
  if(_marks){_marks->Abort("display configuration changed");_marks->RetireLiveShot();}
  _displays=seethis::platform::SnapshotDisplays();
  if(_marks)_marks->SetDisplayObservation(_displays);
  [_noteVisibility reconcile];
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
  [_noteVisibility reconcile];
  _hoveredID.clear();
  [self invalidateNoteBadges];
  _pendingHoverPresentationDisplays.clear();
  for(STOverlayPanel* panel in self.panels)
    [panel.contentView setNeedsDisplay:YES];
  [self updatePointerPolicy:_lastPointer];
}
- (void)publishForegroundUnavailable {
  seethis::platform::PublishChromeObservation(0,false,
      seethis::core::PageAvailability::kUnavailable);
  [_noteVisibility publishWindow:{
      seethis::core::WindowAvailability::kUnavailable,
      ++_windowObservationGeneration,seethis::core::Now(),0,"",0,{}}];
  const auto page_generation=_chromeLifecycle.Invalidate();
  [_noteVisibility publishPage:{
      seethis::core::PageAvailability::kUnavailable,page_generation,
      seethis::core::Now(),std::nullopt}];
  if(_marks->Pending())
    [self abortPending:@"foreground application changed"];
  _captureContext.reset();
  [self invalidateVisibilityPresentation];
  if([self inspectorVisible])[self refreshInspector];
}
- (BOOL)ownsPresentationInput {
  if(NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier!=NSProcessInfo.processInfo.processIdentifier)return NO;
  if(STOwnPanelKeyboardInput(_noteEditor.presentationOpen,_noteEditor.panel.isVisible,
      _noteEditor.panel.isKeyWindow,_inspectorPanel.isVisible,_inspectorPanel.isKeyWindow))return YES;
  if(!_inspectorPanel.isVisible)return NO;
  const NSPoint pointer=NSEvent.mouseLocation;
  return NSPointInRect(pointer,_inspectorPanel.frame)&&
      [NSWindow windowNumberAtPoint:pointer belowWindowWithWindowNumber:0]==_inspectorPanel.windowNumber;
}
- (void)refreshOwnUIPresentationSource {
  const auto target=_marks->own_ui_presentation_source();
  if(!target)return;
  if(![self ownsPresentationInput]){_marks->RetireLiveShot();return;}
  if(_ownSourcePagePending)return;
  auto window=seethis::platform::ObserveOwnUIPresentationSource(target->context,++_windowObservationGeneration);
  if(window.availability!=seethis::core::WindowAvailability::kReady) {
    _marks->RetireLiveShot();[self invalidateVisibilityPresentation];return;
  }
  if(!target->page) {
    (void)STPublishOwnUIPresentation(*_marks,*target,std::move(window),{},[self ownsPresentationInput],_chromePermissionGeneration,seethis::platform::ChromeConnectionStatus().authorization_generation);
    [self invalidateVisibilityPresentation];return;
  }
  _ownSourcePagePending=true;
  _ownSourceRequestGeneration=std::max(_ownSourceRequestGeneration,_marks->page_observation().generation);
  const auto request=++_ownSourceRequestGeneration;
  const auto permission=_chromePermissionGeneration;
  __weak STOverlayCoordinator* weakSelf=self;
  seethis::platform::AcquireChromePageAsync([source=target->context] {
    return seethis::platform::AcquireOwnUIPresentationChromePage(source);
  },request,[weakSelf,target,permission,request](seethis::core::PageObservation page) {
    STOverlayCoordinator* owner=weakSelf;
    if(!owner||request!=owner->_ownSourceRequestGeneration)return;
    owner->_ownSourcePagePending=false;
    const auto active=owner->_marks->own_ui_presentation_source();
    if(!active||active->generation!=target->generation)return;
    if(![owner ownsPresentationInput]||permission!=seethis::platform::ChromeConnectionStatus().authorization_generation) {
      owner->_marks->RetireLiveShot();[owner invalidateVisibilityPresentation];return;
    }
    // Complete with another fresh native read, never the pre-provider window.
    auto fresh=seethis::platform::ObserveOwnUIPresentationSource(target->context,++owner->_windowObservationGeneration);
    (void)STPublishOwnUIPresentation(*owner->_marks,*target,std::move(fresh),std::move(page),[owner ownsPresentationInput],permission,seethis::platform::ChromeConnectionStatus().authorization_generation);
    [owner invalidateVisibilityPresentation];
  });
}
- (void)refreshForegroundWindow {
  // Missing display inventory is absence, not coordinate loss. Re-sample on
  // normal refresh so temporary inventory failure can recover without a new
  // display-change notification; known valid changes are classified in core.
  _marks->SetDisplayObservation(seethis::platform::SnapshotDisplays());
  const bool owned=[self ownsPresentationInput];
  if(owned)STBeginOwnUIPresentation(*_marks);
  _marks->SetOwnInputFocus(owned);
  if(!seethis::platform::ScreenRecordingReady())_marks->RetireLiveShot();
  const auto connection=seethis::platform::RefreshChromeConnection();
  if(connection.authorization_generation!=_chromePermissionGeneration) {
    _chromePermissionGeneration=connection.authorization_generation;
    _marks->RetireLiveShot();
    [_noteVisibility retire];
    const auto page_generation=_chromeLifecycle.Invalidate();
    [_noteVisibility publishPage:{seethis::core::PageAvailability::kUnavailable,
        page_generation,seethis::core::Now(),std::nullopt}];
    [self invalidateVisibilityPresentation];
  }
  if(owned) {
    [self refreshOwnUIPresentationSource];return;
  }
  const auto previous=_marks->window_observation();
  auto current=seethis::platform::ObserveForegroundWindow(
      ++_windowObservationGeneration);
  const bool changed=!STSameWindowIdentity(previous,current);
  [_noteVisibility publishWindow:current];

  if(_marks->Pending() && _captureContext &&
     (!seethis::core::WindowObservationMatchesContext(*_captureContext,current) ||
      (_captureContext->bundle_id!="com.google.Chrome" &&
       seethis::core::CapturedTitleAvailable(*_captureContext) &&
       !seethis::core::TitleContextVisible(*_captureContext,current))))
    [self abortPending:@"foreground window changed"];

  if(changed) {
    const auto page_generation=_chromeLifecycle.Invalidate();
    [_noteVisibility publishPage:{
        seethis::core::PageAvailability::kUnavailable,page_generation,
        seethis::core::Now(),std::nullopt}];
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
    [_noteVisibility publishPage:{
        seethis::core::PageAvailability::kUnavailable,page_generation,
        seethis::core::Now(),std::nullopt}];
    [self invalidateVisibilityPresentation];
  }
  if(const auto target=_marks->own_ui_presentation_source()) {
    if(seethis::core::ReferenceVisible(target->context,target->page,current,_marks->page_observation()))
      _marks->EndOwnUIPresentationSource();
    else if(!seethis::core::WindowObservationMatchesContext(target->context,current))_marks->RetireLiveShot();
  }
  if(!_marks->Pending())_captureContext.reset();
  [_noteVisibility reconcile];
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
    if(!owner || ![owner->_noteVisibility acceptPage:std::move(observation)
        request:request lifecycle:owner->_chromeLifecycle foreground:foreground
        permission:permission_generation
        currentPermission:seethis::platform::ChromeConnectionStatus().authorization_generation])return;
    const auto published=owner->_marks->page_observation();
    if(const auto target=owner->_marks->own_ui_presentation_source()) {
      if(seethis::core::ReferenceVisible(target->context,target->page,owner->_marks->window_observation(),published))
        owner->_marks->EndOwnUIPresentationSource();
      else owner->_marks->RetireLiveShot();
    }
    seethis::platform::PublishChromeObservation(foreground.pid,true,
        published.availability,true);
    if(published.availability==seethis::core::PageAvailability::kReady &&
       published.identity) {
      (void)owner->_marks->BindPageIdentity(capture_generation,
                                            *published.identity,published.captured_page);
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
      owner->_marks->BindPageIdentity(capture_generation,*observation.identity,
                                     observation.captured_page);
    if(capture_generation==owner->_generation)[owner selectAcceptedInspectorCapture];
    for(STOverlayPanel* panel in owner.panels)[panel.contentView setNeedsDisplay:YES];
    [owner updatePointerPolicy:owner->_lastPointer];
  });
#endif
}
- (void)stopChromePageRefresh {
  // Provider/capture cancellation is not an editor lifetime boundary.
  seethis::platform::PublishChromeObservation(0,false,
      seethis::core::PageAvailability::kUnavailable);
  [_chromePageTimer invalidate];_chromePageTimer=nil;_chromeContext.reset();
#if defined(SEETHIS_WINDOW_OBSERVATION_API)
  const auto generation=_chromeLifecycle.Invalidate();
  [_noteVisibility publishPage:{
      seethis::core::PageAvailability::kUnavailable,generation,
      seethis::core::Now(),std::nullopt}];
  [self invalidateVisibilityPresentation];
#else
  _chromeObservationGeneration=0;
#endif
}
- (BOOL)beginCapturePrefix {
  const BOOL accepted=_marks->BeginPresentationPrefix();
  if(accepted){_hoveredID.clear();[self updatePointerPolicy:_lastPointer];}
  return accepted;
}
- (void)confirmCapturePrefix {
  (void)_marks->ConfirmPresentationPrefix();[self updatePointerPolicy:_lastPointer];
}
- (void)cancelCapturePrefix {
  _marks->CancelPresentationPrefix();[self invalidateNoteBadges];_hoveredID.clear();
  for(STOverlayPanel* panel in self.panels)[panel.contentView setNeedsDisplay:YES];
  [self updatePointerPolicy:_lastPointer];
}
- (void)observeViewportInput:(seethis::platform::ViewportInput)event
                   pointer:(const std::optional<seethis::core::DisplayPoint>&)pointer {
  using seethis::platform::ViewportInput;
  bool own=false,ownFocus=false,preservesPresentation=false;
  if(event==ViewportInput::kKeyboard)own=STOwnPanelKeyboardInput(
      _noteEditor.presentationOpen,_noteEditor.panel.isVisible,_noteEditor.panel.isKeyWindow,
      _inspectorPanel.isVisible,_inspectorPanel.isKeyWindow);
  if(pointer && (event==ViewportInput::kPointer||event==ViewportInput::kPrimaryDown||event==ViewportInput::kScroll)) {
    for(STOverlayPanel* panel in self.panels)if(panel.displayID==pointer->display_id) {
      const auto screen=[panel convertRectToScreen:NSMakeRect(pointer->position.x,pointer->position.y,0,0)].origin;
      const NSInteger destination=[NSWindow windowNumberAtPoint:screen belowWindowWithWindowNumber:0];
      own=(_inspectorPanel.isVisible&&destination==_inspectorPanel.windowNumber&&NSPointInRect(screen,_inspectorPanel.frame)) ||
          (_noteEditor.presentationOpen&&_noteEditor.panel.isVisible&&destination==_noteEditor.panel.windowNumber&&NSPointInRect(screen,_noteEditor.panel.frame));
      ownFocus=own;
      if(!own&&(event==ViewportInput::kPointer||event==ViewportInput::kPrimaryDown)) {
        const bool acceptingOverlay=destination==panel.windowNumber&&!panel.ignoresMouseEvents;
        if(acceptingOverlay) {
          own=[self noteBadgeAtPoint:*pointer].has_value();
          if(!own&&!STOwnPanelCoversPoint(*pointer,_displays,[self ownPresentationPanelBounds]))own=STOwnStrokeSelectionInput(acceptingOverlay,
              *_references,*_marks,_settings->Visibility(),_displays,*pointer,
              _settings->Get().mark_hit_radius_points);
        }
      }
      if(!own&&event==ViewportInput::kPrimaryDown) {
        std::vector<std::uint64_t> passThrough;
        for(STOverlayPanel* candidate in self.panels)
          if(candidate.isVisible&&candidate.ignoresMouseEvents&&candidate.windowNumber>0)
            passThrough.push_back(candidate.windowNumber);
        preservesPresentation=STSourceFocusPreservesPresentation(event,pointer,_viewportContinuity.healthy(),
            *_references,*_marks,_settings->Visibility(),_displays,passThrough,
            [&](std::uint64_t below) {
              const NSInteger number=[NSWindow windowNumberAtPoint:screen belowWindowWithWindowNumber:static_cast<NSInteger>(below)];
              return number>0?static_cast<std::uint64_t>(number):0;
            },[&](const seethis::core::Context& source) {
              seethis::core::WindowObservation unavailable;
              const auto currentDisplays=seethis::platform::SnapshotDisplays();
              _marks->SetDisplayObservation(currentDisplays);
              if(!seethis::core::DisplayTopologyMatches(source.displays,currentDisplays)||
                 !STSourceFocusPermissionCurrent(source,_chromePermissionGeneration,
                     seethis::platform::ChromeConnectionStatus().authorization_generation))return unavailable;
              const auto target=_marks->own_ui_presentation_source();
              if(target&&[self ownsPresentationInput]) {
                if(source.pid!=target->context.pid||source.window_id!=target->context.window_id||
                   source.bundle_id!=target->context.bundle_id)return unavailable;
                auto fresh=seethis::platform::ObserveOwnUIPresentationSource(source,++_windowObservationGeneration);
                return [self ownsPresentationInput]&&seethis::core::WindowObservationMatchesContext(target->context,fresh)?fresh:unavailable;
              }
              return seethis::platform::ObserveForegroundWindow(++_windowObservationGeneration);
            });
      }
      break;
    }
  }
  if(ownFocus)STBeginOwnUIPresentation(*_marks);
  STObservePresentationInput(_viewportContinuity,*_marks,event,own,preservesPresentation);
  if(!own&&event!=ViewportInput::kHealthy) {
    [self invalidateNoteBadges];_hoveredID.clear();
    for(STOverlayPanel* panel in self.panels)[panel.contentView setNeedsDisplay:YES];
    [self updatePointerPolicy:_lastPointer];
  }
}
- (void)refresh {
  [self validateAppendMode];
  using namespace seethis::core;
  STRefreshCaptureTransition(*self.controller,_previousState,_generation,
      [&](const auto& snapshot,std::uint64_t& ownedGeneration) {
#if !defined(SEETHIS_WINDOW_OBSERVATION_API)
    [self stopChromePageRefresh];
#endif
    // No drawing or mouse capture is allowed before this synchronous snapshot.
    for(STOverlayPanel* panel in self.panels)panel.ignoresMouseEvents=YES;
    NSString* uuid=[NSUUID.UUID.UUIDString.lowercaseString stringByReplacingOccurrencesOfString:@"-" withString:@""];
    _activeID=uuid.UTF8String;
    _inspectorCaptureSucceeded=false;
    _inspectorCaptureSelectionPending=true;
    _appendCapture=_appendMode.enabled();
    _appendPresentationGeneration=_inspectorLifecycle.selection_generation;
    if(_appendCapture) {
      if(![_noteEditor flushForAppend:_appendMode.root]) {
        _inspectorActionStatus=@"Save the note before adding a screenshot. Draft retained.";
        _inspectorCaptureSelectionPending=false;_appendCapture=false;
        (void)self.controller->Cancel(seethis::core::CancelReason::kUser);
        [self refreshInspector];return;
      }
      const auto start=STBeginModeCapture(*_marks,*_references,_appendMode,uuid.UTF8String,
          _settings->Get().crop_margin_points,snapshot.key_down_monotonic_ms);
      _activeID=start.rootID;_generation=start.generation;ownedGeneration=_generation;
      if(!_generation) {
        _inspectorActionStatus=@"Append reference changed or unavailable. Capture was not added.";
        _inspectorCaptureSelectionPending=false;_appendCapture=false;
        (void)self.controller->Cancel(seethis::core::CancelReason::kUser);
        [self refreshInspector];return;
      }
    } else {
      _generation=STBeginModeCapture(*_marks,*_references,_appendMode,uuid.UTF8String,
          _settings->Get().crop_margin_points,snapshot.key_down_monotonic_ms).generation;
      ownedGeneration=_generation;
    }
    for (const auto& display : _displays) {
      if (display.id == snapshot.session_origin.display_id) {
        self.controller->SetSelectedDisplayGeometry(
            {display.id, {display.logical.x, display.logical.y},
             {display.logical.width, display.logical.height}, display.scale});
        break;
      }
    }
    const auto excluded=STCaptureExcludedWindowIDs(self.panels,_inspectorPanel,_noteEditor.panel);
    std::string error;auto context=seethis::platform::SnapshotContext(snapshot.session_origin,excluded,error);
    if(!context){[self abortPending:[NSString stringWithUTF8String:error.c_str()]];return;}
    else {
      _originalPID=context->pid;
      [self activateInspectorChromeContext:context->bundle_id=="com.google.Chrome"];
      if(_marks->BindContext(_generation,*context)) {
        _marks->BindCaptureDisplay(_generation,snapshot.session_origin.display_id);
        const auto generation=_generation;__weak STOverlayCoordinator* weakSelf=self;
        STObserveAndScheduleCapture(*self.controller,*_marks,_generation,generation,[&] {
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
        },[&] {
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
        });
      } else {[self abortPending:@"Capture context unavailable."];return;}
    }
  },[&](const auto& snapshot) {
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
  },[&](const auto& snapshot) {
#if !defined(SEETHIS_WINDOW_OBSERVATION_API)
    [self stopChromePageRefresh];
#endif
    const auto reason=snapshot.cancel_reason;
    _inspectorCaptureSelectionPending=false;
    _marks->Abort(CancelReasonDescription(reason));_appendCapture=false;
#if defined(SEETHIS_WINDOW_OBSERVATION_API)
    _captureContext.reset();
#endif
    os_log_with_type(OS_LOG_DEFAULT,OS_LOG_TYPE_INFO,
        "seethis capture stage=reference-cancel result=%{public}s",
        CancelReasonName(reason));
  });
  for(STOverlayPanel* panel in self.panels)[panel.contentView setNeedsDisplay:YES];
  if([self inspectorVisible])[self refreshInspector];
}
- (void)refreshGlobalVisibilityPresentation {
  const auto visibility=_settings->Visibility();
  if(_presentedGlobalVisibility==visibility.state)return;
  _presentedGlobalVisibility=visibility.state;
  _marks->SetPresentationPermitted(visibility.permits_presentation());
  [_noteVisibility reconcile];
  [self invalidateNoteBadges];_hoveredID.clear();
  for(STOverlayPanel* panel in self.panels)[panel.contentView setNeedsDisplay:YES];
  [self updatePointerPolicy:_lastPointer];
}
- (void)refreshNoteBadges {
  [self refreshNoteBadgesForVisibility:_settings->Visibility()];
}
- (void)invalidateNoteBadges {
  // The empty reference set is a valid fingerprint, never an invalid-cache
  // sentinel. Drop its payload too before stationary-pointer hit testing.
  _badgeCacheValid=false;
  _badgeFingerprint.clear();_noteBadges.clear();_badgeCheckedAt={};
}
- (void)refreshNoteBadgesForVisibility:(const seethis::core::GlobalVisibilitySnapshot&)visibility {
  [_noteVisibility reconcile];
  if(!visibility.permits_presentation()) {
    [self invalidateNoteBadges];return;
  }
  std::string fingerprint;
  std::vector<std::string> references;
  std::set<std::string> seen;
  const auto add=[&](const std::string& id,int state) {
    fingerprint+=std::to_string(id.size())+":"+id+":"+std::to_string(state)+";";
    if(seen.insert(id).second)references.push_back(id);
  };
  // Re-evaluate current source eligibility before consulting the metadata cache.
  // Numbers share the note owner's lifetime/topology checks and the same live
  // ReferenceVisible observations used by ink and stroke hit testing.
  for(const auto& mark:_marks->marks())
    if(seethis::core::DisplayTopologyMatches(mark.value.context.displays,_displays))
      add(mark.value.id,static_cast<int>(mark.value.first_mark_slot));
  const auto now=std::chrono::steady_clock::now();
  if(_badgeCacheValid && fingerprint==_badgeFingerprint &&
     now-_badgeCheckedAt<std::chrono::milliseconds(250))return;
  _badgeCacheValid=true;
  _badgeFingerprint=std::move(fingerprint);
  _badgeCheckedAt=now;
  _noteBadges.clear();
  for(const auto& id:references) {
    const auto badges=STPresentedNoteBadges(*_references,*_marks,id,visibility,_displays);
    _noteBadges.insert(_noteBadges.end(),badges.begin(),badges.end());
  }
}
- (std::vector<seethis::core::Rect>)ownPresentationPanelBounds {
  std::vector<seethis::core::Rect> bounds;
  for(NSWindow* panel : {static_cast<NSWindow*>(_inspectorPanel),static_cast<NSWindow*>(_noteEditor.panel)}) {
    if(!panel||!panel.isVisible||panel==_noteEditor.panel&&!_noteEditor.presentationOpen)continue;
    const auto frame=panel.frame;bounds.push_back({frame.origin.x,frame.origin.y,frame.size.width,frame.size.height});
  }
  return bounds;
}
- (std::optional<STNoteBadge>)noteBadgeAtPoint:(seethis::core::DisplayPoint)point {
  if(STOwnPanelCoversPoint(point,_displays,[self ownPresentationPanelBounds]))return {};
  if(!_marks->PresentationHitsAllowed())return {};
  [self refreshNoteBadges];
  for(auto it=_noteBadges.rbegin();it!=_noteBadges.rend();++it) {
    if(STNoteBadgeHit(*it,point))return *it;
  }
  return {};
}
- (STDeleteChord)physicalDeleteChord {
  const auto settings=_settings->Get();
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
  return {key_down,flags==required};
}
- (BOOL)handleNoteBadgeClick:(seethis::core::DisplayPoint)point {
  if(self.controller->Snapshot().captures_pointer())return NO;
  const auto badge=[self noteBadgeAtPoint:point];
  if(!badge)return NO;
  const auto chord=[self physicalDeleteChord];
  const bool claimed=_deletion->ConsumeDeleteClick(
      chord.key_down,chord.exact_modifiers);
  if(claimed) {
    const auto deleted=_marks->DeleteJob(badge->reference_id);
    if(badge->mark_id==_selectedMarkID)_selectedMarkID.clear();
    if(badge->reference_id==_inspectorLifecycle.selected_id)
      _inspectorLifecycle=seethis::platform::ReduceInspectorLifecycle(
          _inspectorLifecycle,
          seethis::platform::InspectorLifecycleEvent::kSelectedDelete);
    [self invalidateNoteBadges];
    [self refreshInspector];
    std::cerr<<"seethis: physical-delete stage=number-click id="
      <<badge->reference_id<<" claimed=1 disposition="
      <<DeleteResultName(deleted)<<'\n';
  } else if(!_deletion->Snapshot().blocks_mark_copy()) {
    _appendMode.ReferenceSelected(badge->reference_id);
    if(_inspectorLifecycle.selected_id!=badge->reference_id)
      _inspectorLifecycle=seethis::platform::ReduceInspectorLifecycle(
          _inspectorLifecycle,
          seethis::platform::InspectorLifecycleEvent::kReferenceSelection,
          badge->reference_id);
    _selectedMarkID=badge->mark_id;
    ++_inspectorLifecycle.selection_generation;
    const auto selected=_references->MarkSnapshots(badge->reference_id);
    for(const auto& mark:selected.marks)if(mark.id==badge->mark_id) {
      _previewRootID=badge->reference_id;_previewShotID=mark.shot_id;break;
    }
    [self refreshInspector];
    NSPoint screenPoint=NSEvent.mouseLocation;
    const NSRect rect=STNoteBadgeRect(badge->anchor);
    for(STOverlayPanel* panel in self.panels)
      if(panel.displayID==badge->anchor.display_id) {
        screenPoint=[panel convertRectToScreen:
            NSMakeRect(NSMidX(rect),NSMidY(rect),0,0)].origin;
        break;
      }
    [_noteVisibility toggleReference:badge->reference_id mark:badge->mark_id
                           number:badge->number screenPoint:screenPoint];
  }
  for(STOverlayPanel* panel in self.panels)[panel.contentView setNeedsDisplay:YES];
  [self updatePointerPolicy:point];
  return YES;
}
- (std::optional<std::string>)visibleHitIdentity:(seethis::core::DisplayPoint)point
                                              radius:(double)radius {
  const auto hit=STVisibleStrokeHit(*_references,*_marks,_settings->Visibility(),_displays,point,radius,[self ownPresentationPanelBounds]);
  return hit?std::optional<std::string>(hit->rootID):std::nullopt;
}
- (void)updatePointerPolicy:(const std::optional<seethis::core::DisplayPoint>&)pointer {
  [self validateAppendMode];
  using namespace seethis::core;
  _lastPointer=pointer;
  const auto before=_marks->phase();_marks->Expire(Now());
  if(!_marks->AllowsDrawing()&&self.controller->Snapshot().captures_pointer()&&
     _marks->phase()==ReferencePhase::kAborted)[self abortPending:@"Capture attempt ended. Release and press the shortcut again."];
  if(_marks->Pending() && _originalPID!=NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier)[self abortPending:@"foreground application changed"];
  const bool drawing=self.controller->Snapshot().captures_pointer() && _marks->AllowsDrawing();
  const bool pending=_marks->Pending();
  const auto hit=!pending&&pointer?
      [self visibleHitIdentity:*pointer
                       radius:_settings->Get().mark_hit_radius_points]:
      std::optional<std::string>{};
  const auto badge=pointer?[self noteBadgeAtPoint:*pointer]:
      std::optional<STNoteBadge>{};
  const std::string pointer_id=badge?badge->reference_id:
      hit?*hit:"";
  const auto hit_metadata=!pointer_id.empty()?
      _references->LookupMetadata(pointer_id):
      std::optional<seethis::core::ReferenceJobSnapshot>{};
  const std::string next_hover=pointer_id;
  const bool hover_changed=next_hover!=_hoveredID;
  const std::string previous_hover=_hoveredID;
  _hoveredID=next_hover;
  const auto deletion=_deletion->Snapshot();
  bool pointer_panel_accepts=false;
  for(STOverlayPanel* panel in self.panels) {
    const bool hit_on_panel=!pointer_id.empty()&&
        pointer->display_id==panel.displayID&&
        (badge.has_value() || (hit_metadata&&
         seethis::platform::ReferenceJobAcceptsPointer(hit_metadata->state)));
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
  const auto visibility=_settings->Visibility();
  if(visibility.permits_presentation()) {
  for(const auto& stored:_marks->marks()) {
    const auto& r=stored.value;if(!seethis::core::DisplayTopologyMatches(r.context.displays,_displays))continue;
    const auto visible=STVisibleReferenceRegions(r,_references->MarkSnapshots(r.id),visibility);
    bool paintedHere=false;
    for(const auto& region:visible)seethis::core::ForEachSubpath(region,[&](const auto& path) {
      if(std::any_of(path.begin(),path.end(),[&](const auto& point){return point.display_id==display;}))paintedHere=true;
    });
    if(paintedHere)_marks->RecordPresentedSingle(r,_displays);
    StrokeRegions(visible,display,
           MarkColor(seethis::platform::InspectorMarkIsSelected(
               r.id,_inspectorLifecycle.selected_id)),
           seethis::platform::MarkStrokeWidth(r.id==_hoveredID));
    if(_acknowledgement&&r.id==_acknowledgementID) {
      bool labeled = false;
      for(const auto& region:visible)
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
  [self refreshNoteBadgesForVisibility:visibility];
  for(const auto& badge:_noteBadges) {
    if(badge.anchor.display_id!=display)continue;
    const NSRect frame=STNoteBadgeRect(badge.anchor);
    NSBezierPath* circle=[NSBezierPath bezierPathWithOvalInRect:frame];
    [[NSColor colorWithSRGBRed:0.10 green:0.16 blue:0.23 alpha:0.96] setFill];
    [circle fill];
    [NSColor.whiteColor setStroke];circle.lineWidth=1.5;[circle stroke];
    NSString* number=[NSString stringWithFormat:@"%u",badge.number];
    NSFont* font=[NSFont systemFontOfSize:badge.number>99?10:12
                                     weight:NSFontWeightBold];
    NSDictionary* attributes=@{NSFontAttributeName:font,
        NSForegroundColorAttributeName:NSColor.whiteColor};
    const NSSize size=[number sizeWithAttributes:attributes];
    [number drawAtPoint:NSMakePoint(NSMidX(frame)-size.width/2,
        NSMidY(frame)-size.height/2) withAttributes:attributes];
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
  const auto hit=STVisibleStrokeHit(*_references,*_marks,_settings->Visibility(),
      _displays,point,settings.mark_hit_radius_points,[self ownPresentationPanelBounds]);
  if(!hit||hit->rootID!=_hoveredID)return NO;
  const auto chord=[self physicalDeleteChord];
  const bool claimed=_deletion->ConsumeDeleteClick(
      chord.key_down,chord.exact_modifiers);
  std::optional<seethis::core::DeleteResult> delete_result;
  bool copied=false;
  if(claimed) {
    delete_result=_marks->DeleteJob(hit->rootID);
    if(hit->rootID==_inspectorLifecycle.selected_id)_selectedMarkID.clear();
    _inspectorLifecycle=seethis::platform::InspectorAfterSuccessfulDelete(
        _inspectorLifecycle,*delete_result,_marks->inspector_jobs());
    [self prepareInspectorSelection:_inspectorLifecycle.selected_id];
    _inspectorActionStatus=[NSString stringWithUTF8String:_marks->status().c_str()];
    [self refreshInspector];
  } else if(!_deletion->Snapshot().blocks_mark_copy()) {
    _inspectorActionStatus=nil;
    _appendMode.ReferenceSelected(hit->rootID);
    STSelectStrokeHit(*hit,_inspectorLifecycle,_selectedMarkID,_previewRootID,_previewShotID);
    [self prepareInspectorSelection:_inspectorLifecycle.selected_id];
    copied=[self recopyInspectorSelection:hit->rootID];
    [self showInspectorIfAllowed:seethis::core::InspectorSelectionTrigger::kMarkClick];
  }
  std::cerr<<"seethis: physical-delete stage=native-click id="<<hit->rootID
    <<" physical_key_down="<<chord.key_down
    <<" exact_modifiers="<<chord.exact_modifiers
    <<" claimed="<<claimed<<" disposition="
    <<(delete_result?DeleteResultName(*delete_result):(copied?"copied":"passed"))
    <<'\n';
  for(STOverlayPanel* panel in self.panels)[panel.contentView setNeedsDisplay:YES];
  [self updatePointerPolicy:point];
  return YES;
}
- (BOOL)beginRegion:(seethis::core::DisplayPoint)point {
  if(!self.controller->Snapshot().captures_pointer())return NO;
  const auto observed_ms=std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  const bool began=self.controller->PointerDown(point,observed_ms);
  if(began){[self refresh];[self updatePointerPolicy:point];}
  return YES;
}
- (void)moveRegion:(seethis::core::DisplayPoint)point {
  const auto observed_ms=std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  if(self.controller->PointerMoved(point,observed_ms))[self refresh];
  [self updatePointerPolicy:point];
}
- (void)endRegion:(seethis::core::DisplayPoint)point {
  const auto observed_ms=std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  if(self.controller->PointerUp(point,observed_ms))[self refresh];
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
  void ObserveViewportInput(seethis::platform::ViewportInput event,
      const std::optional<seethis::core::DisplayPoint>& pointer) override {
    [coordinator_ observeViewportInput:event pointer:pointer];
  }
  bool BeginCapturePrefix() override {return [coordinator_ beginCapturePrefix];}
  void ConfirmCapturePrefix() override {[coordinator_ confirmCapturePrefix];}
  void CancelCapturePrefix() override {[coordinator_ cancelCapturePrefix];}
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
#endif  // app-only overlay implementation
#endif  // !SEETHIS_INSPECTOR_FEEDBACK_TESTING
