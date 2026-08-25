// swift-tools-version: 6.0

import PackageDescription

let package = Package(
    name: "VibeBoardBLE",
    platforms: [
        .iOS(.v15),
        .macOS(.v13)
    ],
    products: [
        .library(name: "VibeBoardBLE", targets: ["VibeBoardBLE"])
    ],
    targets: [
        .target(name: "VibeBoardBLE"),
        .testTarget(name: "VibeBoardBLETests", dependencies: ["VibeBoardBLE"])
    ]
)
