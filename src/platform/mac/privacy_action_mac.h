#pragma once
#import <AppKit/AppKit.h>

// Explicit user action only. No reference, window, or local capability data is sent.
@interface STPrivacyPolicyAction : NSObject
- (instancetype)initWithOpener:(BOOL (^)(NSURL*))opener;
- (NSMenuItem*)menuItem;
- (void)openPrivacyPolicy:(id)sender;
@end

@implementation STPrivacyPolicyAction {
  BOOL (^_opener)(NSURL*);
}
- (instancetype)initWithOpener:(BOOL (^)(NSURL*))opener {
  if((self=[super init]))_opener=[opener copy];
  return self;
}
- (NSMenuItem*)menuItem {
  NSMenuItem* item=[[NSMenuItem alloc] initWithTitle:@"Privacy Policy…"
      action:@selector(openPrivacyPolicy:) keyEquivalent:@""];
  item.target=self;
  return item;
}
- (void)openPrivacyPolicy:(id)sender {
  (void)sender;
  NSURL* url=[NSURL URLWithString:@"https://github.com/amingclawdev/seethis/blob/main/PRIVACY.md"];
  if(_opener)_opener(url);
}
@end
