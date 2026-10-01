#import <AppKit/AppKit.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <os/log.h>
#include <algorithm>
#include <cmath>
#include "core/reference.h"

namespace seethis::platform {
namespace {
core::Pixels ReadCapturedPixels(CGImageRef image) {
  if(!image)return {};
  const size_t w=CGImageGetWidth(image),h=CGImageGetHeight(image);
  if(w==0 || h==0 || w>16384 || h>16384 || w*h*4>128ULL*1024*1024)return {};
  core::Pixels pixels{static_cast<std::uint32_t>(w),static_cast<std::uint32_t>(h),core::Bytes(w*h*4)};
  CGColorSpaceRef color=CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CGContextRef bitmap=CGBitmapContextCreate(pixels.rgba.data(),w,h,8,w*4,color,
    static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast)|kCGBitmapByteOrder32Big);
  CGColorSpaceRelease(color);
  if(!bitmap)return {};
  // Drawing CGImage into a default bitmap context preserves its top-left byte
  // row order. A UIKit-style CTM flip here would invert the persisted crop.
  CGContextDrawImage(bitmap,CGRectMake(0,0,w,h),image);CGContextRelease(bitmap);
  // PNG stores straight RGBA; unpremultiply the native bitmap.
  for(size_t p=0;p<pixels.rgba.size();p+=4) {
    const unsigned a=pixels.rgba[p+3];
    for(size_t k=0;k<3;++k)pixels.rgba[p+k]=a?std::min(255U,(unsigned(pixels.rgba[p+k])*255+a/2)/a):0;
  }
  return pixels;
}
}  // namespace
#ifdef SEETHIS_CAPTURE_TESTING
// Test-only owned-value bridge: exercise the actual native helper without capture
// permission, ScreenCaptureKit dispatch or exposing native types in a public API.
core::Pixels NormalizeSyntheticCaptureForTest(const core::Pixels& source) {
  if(!core::ValidPixels(source))return {};
  CGColorSpaceRef color=CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CGDataProviderRef provider=CGDataProviderCreateWithData(nullptr,source.rgba.data(),source.rgba.size(),nullptr);
  CGImageRef image=CGImageCreate(source.width,source.height,8,32,source.width*4,color,
    static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast)|kCGBitmapByteOrder32Big,
    provider,nullptr,false,kCGRenderingIntentDefault);
  auto result=ReadCapturedPixels(image);
  if(image)CGImageRelease(image);
  CGDataProviderRelease(provider);CGColorSpaceRelease(color);return result;
}
#endif
void CaptureDisplay(core::Context context,core::DisplayId display_id,
                    std::function<void(core::CaptureResult)> completion) {
  auto deliver=[completion](core::CaptureResult result) {
    dispatch_async(dispatch_get_main_queue(),^{completion(result);});
  };
  if(@available(macOS 14.2,*)) {
    if(!CGPreflightScreenCaptureAccess()) {deliver({{}, {}, {}, "Screen Recording permission unavailable"});return;}
    // Selection is already immutable. Asynchronous enumeration resolves only
    // the frozen display and overlay exclusion IDs; it never reselects context.
    [SCShareableContent getShareableContentExcludingDesktopWindows:NO onScreenWindowsOnly:NO
      completionHandler:^(SCShareableContent* content,NSError* error) {
        if(error || !content) {deliver({{}, {}, {}, "Display enumeration denied or unavailable"});return;}
        SCDisplay* selected=nil;
        for(SCDisplay* display in content.displays)
          if(display.displayID==display_id)selected=display;
        const auto frozen=std::find_if(context.displays.begin(),context.displays.end(),
          [&](const auto& display){return display.id==display_id;});
        if(!selected||frozen==context.displays.end()) {
          deliver({{}, {}, {}, "Selected display no longer available"});return;
        }
        NSMutableArray<SCWindow*>* excluded=[NSMutableArray array];
        for(SCWindow* window in content.windows) {
          if(std::find(context.excluded_window_ids.begin(),context.excluded_window_ids.end(),
                       window.windowID)!=context.excluded_window_ids.end())
            [excluded addObject:window];
        }
        SCContentFilter* filter=[[SCContentFilter alloc] initWithDisplay:selected
                                                         excludingWindows:excluded];
        SCStreamConfiguration* config=[SCStreamConfiguration new];
        const double width=frozen->pixels.width,height=frozen->pixels.height;
        if(!std::isfinite(width) || !std::isfinite(height) || width<=0 || height<=0 ||
           width>16384 || height>16384 || width*height*4>128ULL*1024*1024) {
          deliver({{}, {}, {}, "Selected display exceeds supported capture size"});return;
        }
        config.width=static_cast<size_t>(width);config.height=static_cast<size_t>(height);
        config.showsCursor=NO;
        config.scalesToFit=NO;config.colorSpaceName=kCGColorSpaceSRGB;
        const core::Stamp requested=core::Now();
        [SCScreenshotManager captureImageWithFilter:filter configuration:config
          completionHandler:^(CGImageRef image,NSError* captureError) {
            const auto callback=core::Now();
            core::CaptureResult result;result.requested=requested;result.completed=callback;
            const size_t w=image?CGImageGetWidth(image):0;
            const size_t h=image?CGImageGetHeight(image):0;
            if(captureError || !image) {
              result.error="Selected display image capture failed or was cancelled";
            } else if(w!=config.width || h!=config.height) {
              result.error="Capture dimensions differ from frozen request";
            } else {
              result.pixels=ReadCapturedPixels(image);
              result.completed=core::Now();
              if(!core::ValidPixels(result.pixels))
                result.error="Capture pixel conversion failed";
            }
            os_log_with_type(OS_LOG_DEFAULT,OS_LOG_TYPE_INFO,
              "seethis pipeline stage=native_capture status=%{public}s "
              "request_callback_us=%{public}lld pixel_conversion_us=%{public}lld "
              "width=%{public}llu height=%{public}llu raw_bytes=%{public}llu",
              result.error.empty()?"ready":"failed",
              static_cast<long long>(callback.monotonic_us-requested.monotonic_us),
              static_cast<long long>(result.completed.monotonic_us-callback.monotonic_us),
              static_cast<unsigned long long>(w),
              static_cast<unsigned long long>(h),
              static_cast<unsigned long long>(result.pixels.rgba.size()));
            deliver(std::move(result));
          }];
      }];
  } else deliver({{}, {}, {}, "Selected-display capture requires macOS 14.2 or later"});
}
}  // namespace seethis::platform
