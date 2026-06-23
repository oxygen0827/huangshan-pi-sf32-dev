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
                    Text("BLE App Install")
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
                    model.refreshSensors()
                } label: {
                    Label("Read Built-in Sensors", systemImage: "sensor.tag.radiowaves.forward")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.bordered)
                .disabled(model.isBusy)

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
