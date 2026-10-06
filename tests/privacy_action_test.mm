#import "../src/platform/mac/privacy_action_mac.h"
#include <iostream>
int main() {
  @autoreleasepool {
    __block int calls=0;
    __block NSURL* received=nil;
    STPrivacyPolicyAction* action=[[STPrivacyPolicyAction alloc] initWithOpener:^BOOL(NSURL* url) {
      ++calls;received=url;return YES;
    }];
    NSMenuItem* item=[action menuItem];
    if(![item.title isEqualToString:@"Privacy Policy…"] || item.target!=action ||
        item.action!=@selector(openPrivacyPolicy:) || calls!=0)return 1;
    // Invoke the same target/action used by the production menu, with no browser or app launch.
    [item.target performSelector:item.action withObject:item];
    if(calls!=1 || ![received.absoluteString isEqualToString:
        @"https://github.com/amingclawdev/seethis/blob/main/PRIVACY.md"] ||
        received.query!=nil || received.fragment!=nil)return 2;
    STPrivacyPolicyAction* absent=[[STPrivacyPolicyAction alloc] initWithOpener:nil];
    [absent openPrivacyPolicy:nil];
    std::cout<<"Production privacy menu action passed without external opener\n";
  }
}
