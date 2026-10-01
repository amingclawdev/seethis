#!/usr/bin/env swift
// Rebuild the editable SVG, 1024 px PNG, and multi-resolution macOS icon together.
import CoreGraphics
import Foundation
import ImageIO
import UniformTypeIdentifiers

let root = URL(fileURLWithPath: #filePath).standardizedFileURL
    .deletingLastPathComponent().deletingLastPathComponent()
let output = root.appendingPathComponent("assets/app-icon", isDirectory: true)
try FileManager.default.createDirectory(at: output, withIntermediateDirectories: true)

let svg = """
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1024 1024">
  <defs>
    <linearGradient id="navy" x1="132" y1="0" x2="896" y2="1024" gradientUnits="userSpaceOnUse">
      <stop stop-color="#193762"/>
      <stop offset="1" stop-color="#07162D"/>
    </linearGradient>
  </defs>
  <rect x="40" y="40" width="944" height="944" rx="204" fill="url(#navy)"/>
  <rect x="70" y="70" width="884" height="884" rx="174" fill="none" stroke="#68A8E9" stroke-opacity=".34" stroke-width="16"/>
  <g fill="none" stroke="#F0F7FF" stroke-width="76" stroke-linecap="round" stroke-linejoin="round">
    <path d="M260 410V286H384"/>
    <path d="M640 286H764V410"/>
    <path d="M260 614V738H384"/>
  </g>
  <circle cx="713" cy="712" r="111" fill="#102747" stroke="#F0F7FF" stroke-width="50"/>
  <circle cx="713" cy="712" r="43" fill="#42C6FF"/>
</svg>
"""
try svg.appending("\n").write(to: output.appendingPathComponent("SeeThis.svg"), atomically: true, encoding: .utf8)

func color(_ red: CGFloat, _ green: CGFloat, _ blue: CGFloat, _ alpha: CGFloat = 1) -> CGColor {
    CGColor(colorSpace: CGColorSpace(name: CGColorSpace.sRGB)!, components: [red / 255, green / 255, blue / 255, alpha])!
}

func render(_ size: Int) -> CGImage {
    let space = CGColorSpace(name: CGColorSpace.sRGB)!
    let context = CGContext(data: nil, width: size, height: size, bitsPerComponent: 8,
                            bytesPerRow: 0, space: space,
                            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
    context.interpolationQuality = .high
    context.translateBy(x: 0, y: CGFloat(size))
    context.scaleBy(x: CGFloat(size) / 1024, y: -CGFloat(size) / 1024)

    let background = CGPath(roundedRect: CGRect(x: 40, y: 40, width: 944, height: 944),
                            cornerWidth: 204, cornerHeight: 204, transform: nil)
    context.saveGState()
    context.addPath(background)
    context.clip()
    let gradient = CGGradient(colorsSpace: space,
                              colors: [color(25, 55, 98), color(7, 22, 45)] as CFArray,
                              locations: [0, 1])!
    context.drawLinearGradient(gradient, start: CGPoint(x: 132, y: 0),
                               end: CGPoint(x: 896, y: 1024), options: [])
    context.restoreGState()

    let border = CGPath(roundedRect: CGRect(x: 70, y: 70, width: 884, height: 884),
                        cornerWidth: 174, cornerHeight: 174, transform: nil)
    context.addPath(border)
    context.setStrokeColor(color(104, 168, 233, 0.34))
    context.setLineWidth(16)
    context.strokePath()

    context.setStrokeColor(color(240, 247, 255))
    context.setLineWidth(76)
    context.setLineCap(.round)
    context.setLineJoin(.round)
    for points in [
        [CGPoint(x: 260, y: 410), CGPoint(x: 260, y: 286), CGPoint(x: 384, y: 286)],
        [CGPoint(x: 640, y: 286), CGPoint(x: 764, y: 286), CGPoint(x: 764, y: 410)],
        [CGPoint(x: 260, y: 614), CGPoint(x: 260, y: 738), CGPoint(x: 384, y: 738)],
    ] {
        context.beginPath()
        context.move(to: points[0])
        for point in points.dropFirst() { context.addLine(to: point) }
        context.strokePath()
    }

    let marker = CGRect(x: 602, y: 601, width: 222, height: 222)
    context.setFillColor(color(16, 39, 71))
    context.fillEllipse(in: marker)
    context.setStrokeColor(color(240, 247, 255))
    context.setLineWidth(50)
    context.strokeEllipse(in: marker)
    context.setFillColor(color(66, 198, 255))
    context.fillEllipse(in: CGRect(x: 670, y: 669, width: 86, height: 86))
    return context.makeImage()!
}

func savePNG(_ image: CGImage, to url: URL) {
    let destination = CGImageDestinationCreateWithURL(url as CFURL, UTType.png.identifier as CFString, 1, nil)!
    CGImageDestinationAddImage(destination, image, nil)
    precondition(CGImageDestinationFinalize(destination), "Could not write \(url.path)")
}

savePNG(render(1024), to: output.appendingPathComponent("SeeThis.png"))
let temporary = FileManager.default.temporaryDirectory.appendingPathComponent("seethis-icon-\(UUID().uuidString).iconset")
try FileManager.default.createDirectory(at: temporary, withIntermediateDirectories: true)
defer { try? FileManager.default.removeItem(at: temporary) }
for (name, size) in [
    ("icon_16x16.png", 16), ("icon_16x16@2x.png", 32),
    ("icon_32x32.png", 32), ("icon_32x32@2x.png", 64),
    ("icon_128x128.png", 128), ("icon_128x128@2x.png", 256),
    ("icon_256x256.png", 256), ("icon_256x256@2x.png", 512),
    ("icon_512x512.png", 512), ("icon_512x512@2x.png", 1024),
] {
    savePNG(render(size), to: temporary.appendingPathComponent(name))
}
let iconutil = Process()
iconutil.executableURL = URL(fileURLWithPath: "/usr/bin/iconutil")
iconutil.arguments = ["-c", "icns", "-o", output.appendingPathComponent("SeeThis.icns").path, temporary.path]
try iconutil.run()
iconutil.waitUntilExit()
guard iconutil.terminationStatus == 0 else { fatalError("iconutil failed") }
print("Generated \(output.path)/SeeThis.{svg,png,icns}")
