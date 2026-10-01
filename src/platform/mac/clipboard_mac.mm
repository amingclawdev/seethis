#import <AppKit/AppKit.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "platform/platform.h"

namespace seethis::platform {
namespace {

bool IsReferenceJsonUrl(std::string_view text) {
  constexpr std::string_view kOrigin = "http://127.0.0.1:";
  constexpr std::string_view kPath = "/r/";
  if (text.empty() || text.size() >= 2048 || !text.starts_with(kOrigin))
    return false;
  const auto path_start = text.find('/', kOrigin.size());
  if (path_start == std::string_view::npos) return false;
  const auto port = text.substr(kOrigin.size(), path_start - kOrigin.size());
  if (port.empty() || port.size() > 5) return false;
  unsigned port_number = 0;
  for (const char digit : port) {
    if (digit < '0' || digit > '9') return false;
    port_number = port_number * 10 + static_cast<unsigned>(digit - '0');
  }
  if (port_number == 0 || port_number > 65535) return false;
  if (!text.substr(path_start).starts_with(kPath)) return false;
  const auto handle = text.substr(path_start + kPath.size());
  if (handle.size() != 96) return false;
  for (const char digit : handle) {
    if (!((digit >= '0' && digit <= '9') ||
          (digit >= 'a' && digit <= 'f'))) return false;
  }
  return true;
}

constexpr std::size_t kMaximumAnnotatedInputBytes = 16 * 1024 * 1024;
constexpr std::uint64_t kMaximumAnnotatedDecodedBytes = 64 * 1024 * 1024;
constexpr std::size_t kMaximumAnnotatedOutputBytes = 20 * 1024 * 1024;
constexpr std::uint32_t kMaximumAnnotatedSide = 2048;

std::uint32_t ReadBigEndian32(const std::uint8_t* bytes) {
  return (std::uint32_t(bytes[0]) << 24) |
         (std::uint32_t(bytes[1]) << 16) |
         (std::uint32_t(bytes[2]) << 8) | bytes[3];
}

struct PreparedAnnotatedPng {
  AnnotatedCopyResult result;
  core::Bytes bytes;
};

struct AnnotatedCopyOperation {
  std::chrono::steady_clock::time_point deadline;
  std::atomic<bool> cancelled{false};
  bool completed = false;  // Main queue only.
  std::function<void(AnnotatedCopyResult)> completion;
  std::string pasteboard_name;
  std::int64_t expected_change_count = 0;
};

AnnotatedCopyResult CopyStatus(AnnotatedCopyStatus status) {
  AnnotatedCopyResult result;
  result.status = status;
  return result;
}

bool Expired(const AnnotatedCopyOperation& operation) {
  return operation.cancelled.load(std::memory_order_acquire) ||
         std::chrono::steady_clock::now() >= operation.deadline;
}

PreparedAnnotatedPng PrepareAnnotatedPng(
    const core::Bytes& png, const AnnotatedCopyOperation& operation) {
  PreparedAnnotatedPng prepared;
  if (Expired(operation)) {
    prepared.result.status = AnnotatedCopyStatus::kTimedOut;
    return prepared;
  }
  if (png.size() > kMaximumAnnotatedInputBytes) {
    prepared.result.status = AnnotatedCopyStatus::kTooLarge;
    return prepared;
  }
  constexpr std::uint8_t kSignature[] = {137, 80, 78, 71, 13, 10, 26, 10};
  if (png.size() < 33 || std::memcmp(png.data(), kSignature, 8) != 0 ||
      ReadBigEndian32(png.data() + 8) != 13 ||
      std::memcmp(png.data() + 12, "IHDR", 4) != 0 ||
      png[24] != 8 || png[25] != 6 || png[26] != 0 || png[27] != 0 ||
      png[28] != 0) {
    prepared.result.status = AnnotatedCopyStatus::kInvalidPng;
    return prepared;
  }
  const auto width = ReadBigEndian32(png.data() + 16);
  const auto height = ReadBigEndian32(png.data() + 20);
  // PNG dimensions are at most 2^31-1. Reject larger IHDR values before any
  // decoder sees them, including values that could overflow a byte product.
  if (width == 0 || height == 0 || width > 0x7fffffffU ||
      height > 0x7fffffffU) {
    prepared.result.status = AnnotatedCopyStatus::kInvalidPng;
    return prepared;
  }
  constexpr auto kMaximumAnnotatedPixels = kMaximumAnnotatedDecodedBytes / 4;
  if (width > kMaximumAnnotatedPixels / height) {
    prepared.result.status = AnnotatedCopyStatus::kTooLarge;
    return prepared;
  }
  prepared.result.width = width;
  prepared.result.height = height;
  @autoreleasepool {
    NSData* data = [NSData dataWithBytesNoCopy:const_cast<std::uint8_t*>(png.data())
                                  length:png.size() freeWhenDone:NO];
    NSBitmapImageRep* source = [[NSBitmapImageRep alloc] initWithData:data];
    if (!source || source.pixelsWide != width || source.pixelsHigh != height) {
      prepared.result.status = AnnotatedCopyStatus::kInvalidPng;
      return prepared;
    }
    if (Expired(operation)) {
      prepared.result.status = AnnotatedCopyStatus::kTimedOut;
      return prepared;
    }
    const auto longest = std::max(width, height);
    if (longest <= kMaximumAnnotatedSide) {
      prepared.bytes = png;
    } else {
      const auto scaled = [longest](std::uint32_t side) {
        return static_cast<std::uint32_t>(std::max<std::uint64_t>(
            1, (std::uint64_t(side) * kMaximumAnnotatedSide) / longest));
      };
      const auto output_width = scaled(width);
      const auto output_height = scaled(height);
      NSBitmapImageRep* output = [[NSBitmapImageRep alloc]
          initWithBitmapDataPlanes:nullptr
                       pixelsWide:output_width pixelsHigh:output_height
                    bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES
                         isPlanar:NO colorSpaceName:NSCalibratedRGBColorSpace
                      bytesPerRow:output_width * 4 bitsPerPixel:32];
      if (!output || !source.CGImage) {
        prepared.result.status = AnnotatedCopyStatus::kInvalidPng;
        return prepared;
      }
      NSGraphicsContext* graphics =
          [NSGraphicsContext graphicsContextWithBitmapImageRep:output];
      if (!graphics) {
        prepared.result.status = AnnotatedCopyStatus::kInvalidPng;
        return prepared;
      }
      [NSGraphicsContext saveGraphicsState];
      [NSGraphicsContext setCurrentContext:graphics];
      CGContextRef context = graphics.CGContext;
      CGContextSetInterpolationQuality(context, kCGInterpolationHigh);
      CGContextClearRect(context, CGRectMake(0, 0, output_width, output_height));
      CGContextDrawImage(context, CGRectMake(0, 0, output_width, output_height),
                         source.CGImage);
      [NSGraphicsContext restoreGraphicsState];
      if (Expired(operation)) {
        prepared.result.status = AnnotatedCopyStatus::kTimedOut;
        return prepared;
      }
      NSData* encoded = [output representationUsingType:NSBitmapImageFileTypePNG
                                              properties:@{}];
      if (!encoded) {
        prepared.result.status = AnnotatedCopyStatus::kInvalidPng;
        return prepared;
      }
      if (encoded.length > kMaximumAnnotatedOutputBytes) {
        prepared.result.status = AnnotatedCopyStatus::kTooLarge;
        return prepared;
      }
      const auto* begin = static_cast<const std::uint8_t*>(encoded.bytes);
      prepared.bytes.assign(begin, begin + encoded.length);
      prepared.result.width = output_width;
      prepared.result.height = output_height;
      prepared.result.downscaled = true;
    }
  }
  if (prepared.bytes.empty() ||
      prepared.bytes.size() > kMaximumAnnotatedOutputBytes) {
    prepared.result.status = AnnotatedCopyStatus::kTooLarge;
    prepared.bytes.clear();
    return prepared;
  }
  prepared.result.status = Expired(operation) ? AnnotatedCopyStatus::kTimedOut
                                              : AnnotatedCopyStatus::kCopied;
  if (prepared.result.status != AnnotatedCopyStatus::kCopied)
    prepared.bytes.clear();
  return prepared;
}

void FinishAnnotatedCopy(const std::shared_ptr<AnnotatedCopyOperation>& operation,
                         AnnotatedCopyResult result) {
  if (operation->completed) return;
  operation->completed = true;
  operation->cancelled.store(true, std::memory_order_release);
  operation->completion(result);
}

}  // namespace

void CopyAnnotatedPng(std::shared_ptr<const core::Bytes> png,
                      std::chrono::milliseconds timeout,
                      std::function<void(AnnotatedCopyResult)> completion,
                      std::string pasteboard_name) {
  if (!completion) return;
  if (![NSThread isMainThread]) {
    dispatch_async(dispatch_get_main_queue(), ^{
      completion(CopyStatus(AnnotatedCopyStatus::kPasteboardFailed));
    });
    return;
  }
  if (timeout <= std::chrono::milliseconds::zero()) {
    completion(CopyStatus(AnnotatedCopyStatus::kTimedOut));
    return;
  }
  if (!png || png->empty()) {
    completion(CopyStatus(AnnotatedCopyStatus::kInvalidPng));
    return;
  }
  if (png->size() > kMaximumAnnotatedInputBytes) {
    completion(CopyStatus(AnnotatedCopyStatus::kTooLarge));
    return;
  }
  NSPasteboard* initial_pasteboard = pasteboard_name.empty()
      ? NSPasteboard.generalPasteboard
      : [NSPasteboard pasteboardWithName:
            [NSString stringWithUTF8String:pasteboard_name.c_str()]];
  if (!initial_pasteboard) {
    completion(CopyStatus(AnnotatedCopyStatus::kPasteboardFailed));
    return;
  }
  const auto bounded_timeout = std::min(timeout, std::chrono::milliseconds(10000));
  auto operation = std::make_shared<AnnotatedCopyOperation>();
  operation->deadline = std::chrono::steady_clock::now() + bounded_timeout;
  operation->completion = std::move(completion);
  operation->pasteboard_name = std::move(pasteboard_name);
  operation->expected_change_count = initial_pasteboard.changeCount;
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW,
                               bounded_timeout.count() * NSEC_PER_MSEC),
                 dispatch_get_main_queue(), ^{
    FinishAnnotatedCopy(operation, CopyStatus(AnnotatedCopyStatus::kTimedOut));
  });
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
    auto prepared = std::make_shared<PreparedAnnotatedPng>(
        PrepareAnnotatedPng(*png, *operation));
    dispatch_async(dispatch_get_main_queue(), ^{
      if (operation->completed) return;
      if (Expired(*operation)) {
        FinishAnnotatedCopy(operation, CopyStatus(AnnotatedCopyStatus::kTimedOut));
        return;
      }
      if (prepared->result.status != AnnotatedCopyStatus::kCopied) {
        FinishAnnotatedCopy(operation, prepared->result);
        return;
      }
      @autoreleasepool {
        NSPasteboard* pasteboard = operation->pasteboard_name.empty()
            ? NSPasteboard.generalPasteboard
            : [NSPasteboard pasteboardWithName:
                  [NSString stringWithUTF8String:operation->pasteboard_name.c_str()]];
        NSPasteboardItem* item = [[NSPasteboardItem alloc] init];
        NSData* data = [NSData dataWithBytes:prepared->bytes.data()
                                     length:prepared->bytes.size()];
        if (!pasteboard || !data || ![item setData:data forType:NSPasteboardTypePNG]) {
          prepared->result.status = AnnotatedCopyStatus::kPasteboardFailed;
        } else if (Expired(*operation)) {
          prepared->result.status = AnnotatedCopyStatus::kTimedOut;
        } else if (pasteboard.changeCount != operation->expected_change_count) {
          prepared->result.status = AnnotatedCopyStatus::kPasteboardChanged;
        } else {
          [pasteboard clearContents];
          if (![pasteboard writeObjects:@[item]]) {
            prepared->result.status = AnnotatedCopyStatus::kPasteboardFailed;
          } else {
            prepared->result.change_count =
                static_cast<std::int64_t>(pasteboard.changeCount);
          }
        }
      }
      FinishAnnotatedCopy(operation, prepared->result);
    });
  });
}

std::optional<std::int64_t> CopyReferenceText(std::string_view text) {
  constexpr auto plan = PlanReferenceClipboardWrite(
      ReferenceClipboardTrigger::kDeliberatePublication);
  static_assert(plan.writes_pasteboard && plan.plain_text_only &&
                plan.representation_count == 1);
  if (![NSThread isMainThread] || !IsReferenceJsonUrl(text)) return {};
  @autoreleasepool {
    NSString* value = [[NSString alloc] initWithBytes:text.data()
                                                length:text.size()
                                              encoding:NSUTF8StringEncoding];
    if (!value) return {};
    NSPasteboard* pasteboard = NSPasteboard.generalPasteboard;
    [pasteboard clearContents];
    if (![pasteboard setString:value forType:NSPasteboardTypeString]) return {};
    return static_cast<std::int64_t>(pasteboard.changeCount);
  }
}

bool CompleteReferenceClipboard(
    std::shared_ptr<const core::StoredReference> reference,
    std::string_view text, std::int64_t expected_change_count) {
  constexpr auto plan = PlanReferenceClipboardWrite(
      ReferenceClipboardTrigger::kAsyncCompletion);
  static_assert(!plan.writes_pasteboard && !plan.plain_text_only &&
                plan.representation_count == 0 &&
                !plan.proves_content_present);
  (void)reference;
  (void)text;
  (void)expected_change_count;
  return plan.proves_content_present;
}

bool CopyReference(const core::StoredReference& reference) {
  const auto& bytes = reference.agent_clipboard_text.empty()
      ? reference.clipboard_text : reference.agent_clipboard_text;
  const std::string_view text(
      reinterpret_cast<const char*>(bytes.data()), bytes.size());
  if (!IsReferenceJsonUrl(text)) return false;
  return CopyReferenceText(text).has_value();
}
}  // namespace seethis::platform
