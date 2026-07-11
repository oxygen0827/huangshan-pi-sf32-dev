import SwiftUI
import UniformTypeIdentifiers

public struct VibeBoardDemoView: View {
    @Environment(\.scenePhase) private var scenePhase
    @StateObject private var model = VibeBoardDemoModel()
    @State private var isImportingPackage = false

    public init() {}

    public var body: some View {
        NavigationView {
            VStack(alignment: .leading, spacing: 20) {
                VStack(alignment: .leading, spacing: 8) {
                    Text("VibeBoard")
                        .font(.system(.largeTitle, design: .rounded).bold())
                    Text("BLE Runtime Console")
                        .font(.headline)
                        .foregroundStyle(.secondary)
                }

                Text(model.statusText)
                    .font(.system(.body, design: .monospaced))
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(14)
                    .background(.thinMaterial)
                    .clipShape(RoundedRectangle(cornerRadius: 8))

                HStack(spacing: 8) {
                    Image(systemName: "dot.radiowaves.left.and.right")
                    Text(model.connectionText)
                        .font(.footnote)
                        .lineLimit(2)
                }
                .foregroundStyle(.secondary)

                Button {
                    model.connect()
                } label: {
                    Label("Connect / Auto Reconnect", systemImage: "antenna.radiowaves.left.and.right")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
                .disabled(model.isBusy)

                Button {
                    model.refreshStatus()
                } label: {
                    Label("Read Runtime Status", systemImage: "waveform.path.ecg")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.bordered)
                .disabled(model.isBusy)

                Button {
                    model.refreshCapabilities()
                } label: {
                    Label("Read Capabilities", systemImage: "checklist")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.bordered)
                .disabled(model.isBusy)

                Button {
                    model.refreshSensors()
                } label: {
                    Label("Read Built-in Sensors", systemImage: "sensor.tag.radiowaves.forward")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.bordered)
                .disabled(model.isBusy)

                Button {
                    model.refreshPower()
                } label: {
                    Label("Read Power", systemImage: "battery.100")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.bordered)
                .disabled(model.isBusy)

                HStack(spacing: 8) {
                    Button {
                        model.refreshTouch()
                    } label: {
                        Label("Read Touch", systemImage: "hand.tap")
                            .frame(maxWidth: .infinity)
                    }
                    .buttonStyle(.bordered)
                    .disabled(model.isBusy)

                    Button {
                        model.refreshGPIO()
                    } label: {
                        Label("Read GPIO", systemImage: "button.programmable")
                            .frame(maxWidth: .infinity)
                    }
                    .buttonStyle(.bordered)
                    .disabled(model.isBusy)
                }

                VStack(alignment: .leading, spacing: 6) {
                    HStack(spacing: 8) {
                        Text("Touch \(model.latestTouchCount)")
                            .font(.caption.monospacedDigit())
                            .foregroundStyle(.secondary)
                        Spacer()
                        Text("\(model.latestTouchEvent) @ \(model.latestTouchPoint)")
                            .font(.caption.monospacedDigit())
                            .foregroundStyle(.secondary)
                            .lineLimit(1)
                    }
                    HStack(spacing: 8) {
                        Text("\(model.latestTouchGesture)")
                            .font(.caption.monospacedDigit())
                            .foregroundStyle(.secondary)
                        Spacer()
                        Text("Δ \(model.latestTouchDelta) • \(model.latestTouchDurationMs) ms")
                            .font(.caption.monospacedDigit())
                            .foregroundStyle(.secondary)
                            .lineLimit(1)
                    }
                    Text(model.latestGPIOStatus)
                        .font(.caption.monospacedDigit())
                        .foregroundStyle(.secondary)
                        .lineLimit(2)
                }


                VStack(alignment: .leading, spacing: 10) {
                    TextField("Display brightness 0-100", text: $model.displayBrightness)
                        .textFieldStyle(.roundedBorder)
                        .disabled(model.isBusy)

                    HStack(spacing: 8) {
                        Button {
                            model.setDisplayBrightness()
                        } label: {
                            Label("Set Display", systemImage: "sun.max")
                                .frame(maxWidth: .infinity)
                        }
                        .buttonStyle(.borderedProminent)
                        .disabled(model.isBusy || model.displayBrightness.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)

                        Button {
                            model.refreshDisplay()
                        } label: {
                            Label("Read Display", systemImage: "display")
                                .frame(maxWidth: .infinity)
                        }
                        .buttonStyle(.bordered)
                        .disabled(model.isBusy)
                    }
                }

                VStack(alignment: .leading, spacing: 10) {
                    TextField("RGB color (off/red/3366ff)", text: $model.rgbColor)
                        .textFieldStyle(.roundedBorder)
                        .disabled(model.isBusy)

                    HStack(spacing: 8) {
                        Button {
                            model.setRGB()
                        } label: {
                            Label("Set RGB", systemImage: "lightbulb.max")
                                .frame(maxWidth: .infinity)
                        }
                        .buttonStyle(.borderedProminent)
                        .disabled(model.isBusy || model.rgbColor.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)

                        Button {
                            model.refreshRGB()
                        } label: {
                            Label("Read RGB", systemImage: "eyedropper")
                                .frame(maxWidth: .infinity)
                        }
                        .buttonStyle(.bordered)
                        .disabled(model.isBusy)
                    }
                }

                VStack(alignment: .leading, spacing: 10) {
                    Text("App Manager")
                        .font(.headline)

                    TextField("Runtime app id", text: $model.appManagerAppId)
                        .textFieldStyle(.roundedBorder)
                        .disabled(model.isBusy)

                    HStack(spacing: 8) {
                        Button {
                            model.launchApp()
                        } label: {
                            Label("Launch", systemImage: "play.fill")
                                .frame(maxWidth: .infinity)
                        }
                        .buttonStyle(.borderedProminent)
                        .disabled(model.isBusy || model.appManagerAppId.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)

                        Button {
                            model.stopApp()
                        } label: {
                            Label("Stop", systemImage: "stop.fill")
                                .labelStyle(.iconOnly)
                        }
                        .buttonStyle(.bordered)
                        .disabled(model.isBusy)

                        Button {
                            model.deleteApp()
                        } label: {
                            Label("Delete", systemImage: "trash")
                                .labelStyle(.iconOnly)
                        }
                        .buttonStyle(.bordered)
                        .disabled(model.isBusy || model.appManagerAppId.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
                    }

                    HStack(spacing: 8) {
                        Button {
                            model.refreshAppManagerStatus()
                        } label: {
                            Label("App", systemImage: "rectangle.stack")
                                .frame(maxWidth: .infinity)
                        }
                        .buttonStyle(.bordered)
                        .disabled(model.isBusy)

                        Button {
                            model.refreshInstalledApps()
                        } label: {
                            Label("Apps", systemImage: "list.bullet.rectangle.portrait")
                                .frame(maxWidth: .infinity)
                        }
                        .buttonStyle(.bordered)
                        .disabled(model.isBusy)
                    }

                    Text(model.latestAppManagerStatus)
                        .font(.caption.monospacedDigit())
                        .foregroundStyle(.secondary)
                        .lineLimit(2)
                    Text(model.installedAppsText)
                        .font(.caption.monospacedDigit())
                        .foregroundStyle(.secondary)
                        .lineLimit(3)
                }

                VStack(alignment: .leading, spacing: 10) {
                    TextField("Message to board", text: $model.infoFlowText)
                        .textFieldStyle(.roundedBorder)
                        .disabled(model.isBusy)

                    HStack(spacing: 8) {
                        Button {
                            model.sendInfoFlow()
                        } label: {
                            Label("Send Flow", systemImage: "paperplane")
                                .frame(maxWidth: .infinity)
                        }
                        .buttonStyle(.borderedProminent)
                        .disabled(model.isBusy || model.infoFlowText.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)

                        Button {
                            model.refreshInfoFlowStatus()
                        } label: {
                            Label("Flow", systemImage: "list.bullet.rectangle")
                                .labelStyle(.iconOnly)
                        }
                        .buttonStyle(.bordered)
                        .disabled(model.isBusy)

                        Button {
                            model.clearInfoFlow()
                        } label: {
                            Label("Clear Flow", systemImage: "trash")
                                .labelStyle(.iconOnly)
                        }
                        .buttonStyle(.bordered)
                        .disabled(model.isBusy)
                    }
                }

                VStack(alignment: .leading, spacing: 10) {
                    HStack(spacing: 8) {
                        Text("Voice")
                            .font(.headline)
                        Spacer()
                        Text("seq \(model.voiceSequence) • \(model.latestVoicePCMBytes) bytes")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }

                    HStack(spacing: 8) {
                        Text("\(Int(model.voiceDurationMs.rounded())) ms")
                            .font(.caption.monospacedDigit())
                            .foregroundStyle(.secondary)
                            .frame(width: 72, alignment: .leading)
                        Slider(value: $model.voiceDurationMs, in: 300...3000, step: 100)
                            .disabled(model.isBusy)
                    }

                    HStack(spacing: 8) {
                        Button {
                            model.captureVoice()
                        } label: {
                            Label("Capture", systemImage: "waveform.badge.mic")
                                .frame(maxWidth: .infinity)
                        }
                        .buttonStyle(.borderedProminent)
                        .disabled(model.isBusy)

                        Button {
                            model.refreshVoiceStatus()
                        } label: {
                            Label("Status", systemImage: "mic")
                                .labelStyle(.iconOnly)
                        }
                        .buttonStyle(.bordered)
                        .disabled(model.isBusy)

                        Button {
                            model.clearVoice()
                        } label: {
                            Label("Clear Voice", systemImage: "trash")
                                .labelStyle(.iconOnly)
                        }
                        .buttonStyle(.bordered)
                        .disabled(model.isBusy)
                    }

                    TextField("Voice reply to board", text: $model.voiceReplyText)
                        .textFieldStyle(.roundedBorder)
                        .disabled(model.isBusy)

                    Button {
                        model.sendVoiceReply()
                    } label: {
                        Label("Send Voice Reply", systemImage: "message.badge.waveform")
                            .frame(maxWidth: .infinity)
                    }
                    .buttonStyle(.bordered)
                    .disabled(model.isBusy || model.voiceReplyText.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
                }

                Button {
                    model.installDemoApp()
                } label: {
                    Label("Install Demo App Over BLE", systemImage: "square.and.arrow.down")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.bordered)
                .disabled(model.isBusy)

                Button {
                    isImportingPackage = true
                } label: {
                    Label("Import App Folder Over BLE", systemImage: "folder.badge.plus")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.bordered)
                .disabled(model.isBusy)

                Text(model.latestPackageSummary)
                    .font(.caption.monospacedDigit())
                    .foregroundStyle(.secondary)
                    .lineLimit(2)

                Spacer()
            }
            .padding(20)
            .navigationTitle("Huangshan")
        }
        .onAppear {
            model.autoConnectIfNeeded()
        }
        .onChange(of: scenePhase) { phase in
            if phase == .active {
                model.autoConnectIfNeeded()
            }
        }
        .fileImporter(
            isPresented: $isImportingPackage,
            allowedContentTypes: [.folder],
            allowsMultipleSelection: false
        ) { result in
            switch result {
            case .success(let urls):
                guard let directory = urls.first else { return }
                model.installPackage(from: directory)
            case .failure(let error):
                model.showError(error)
            }
        }
    }
}

struct VibeBoardDemoView_Previews: PreviewProvider {
    static var previews: some View {
        VibeBoardDemoView()
    }
}
