import ApplicationServices
import Foundation

struct ElementRecord: Codable {
    let index: Int
    let roleError: Int32
    let role: String?
    let pidError: Int32
    let ownerPidMatches: Bool
    let equalsRoot: Bool
}

struct AttributeRecord: Codable {
    let attribute: String
    let error: Int32
    let returnedCount: Int
    let truncated: Bool
    let elements: [ElementRecord]
}

struct Report: Codable {
    let targetPid: Int32
    let messagingTimeoutError: Int32
    let attributes: [AttributeRecord]
}

guard CommandLine.arguments.count == 2, let targetPid = pid_t(CommandLine.arguments[1]) else { exit(2) }
guard AXIsProcessTrusted() else { exit(3) }
let app = AXUIElementCreateApplication(targetPid)
var ownerPid: pid_t = 0
guard AXUIElementGetPid(app, &ownerPid) == .success, ownerPid == targetPid else { exit(4) }
let timeoutError = AXUIElementSetMessagingTimeout(app, 1.0)
let names = [kAXWindowsAttribute, kAXChildrenAttribute, kAXFocusedWindowAttribute]
var records: [AttributeRecord] = []
for name in names {
    var value: CFTypeRef?
    let error = AXUIElementCopyAttributeValue(app, name as CFString, &value)
    var returned: [AXUIElement] = []
    if error == .success, let value {
        if CFGetTypeID(value) == AXUIElementGetTypeID() {
            returned = [unsafeBitCast(value, to: AXUIElement.self)]
        } else if CFGetTypeID(value) == CFArrayGetTypeID(), let array = value as? [AXUIElement] {
            returned = array
        }
    }
    let limited = Array(returned.prefix(4))
    let elements = limited.enumerated().map { index, element -> ElementRecord in
        var roleValue: CFTypeRef?
        let roleError = AXUIElementCopyAttributeValue(element, kAXRoleAttribute as CFString, &roleValue)
        let exactRole = roleError == .success ? roleValue.map(String.init(describing:)) : nil
        var pid: pid_t = 0
        let pidError = AXUIElementGetPid(element, &pid)
        return ElementRecord(index: index, roleError: roleError.rawValue, role: exactRole,
                             pidError: pidError.rawValue,
                             ownerPidMatches: pidError == .success && pid == targetPid,
                             equalsRoot: CFEqual(element, app))
    }
    records.append(AttributeRecord(attribute: name, error: error.rawValue,
                                   returnedCount: returned.count, truncated: returned.count > limited.count,
                                   elements: elements))
}
let report = Report(targetPid: targetPid, messagingTimeoutError: timeoutError.rawValue, attributes: records)
let encoder = JSONEncoder()
encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
FileHandle.standardOutput.write(try encoder.encode(report))
FileHandle.standardOutput.write(Data("\n".utf8))
