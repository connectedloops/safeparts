import ApplicationServices
import Foundation

func attribute(_ element: AXUIElement, _ name: String) -> CFTypeRef? {
    var value: CFTypeRef?
    guard AXUIElementCopyAttributeValue(element, name as CFString, &value) == .success else { return nil }
    return value
}

func text(_ element: AXUIElement, _ name: String) -> String {
    guard let value = attribute(element, name) else { return "" }
    return String(describing: value)
}

func descendants(_ root: AXUIElement) -> [AXUIElement] {
    var result: [AXUIElement] = []
    var pending = [root]
    while let element = pending.popLast() {
        result.append(element)
        if let children = attribute(element, kAXChildrenAttribute) as? [AXUIElement] {
            pending.append(contentsOf: children.reversed())
        }
    }
    return result
}

func matches(_ element: AXUIElement, role: String, title: String) -> Bool {
    text(element, kAXRoleAttribute) == role &&
        (text(element, kAXTitleAttribute) == title || text(element, kAXDescriptionAttribute) == title || text(element, kAXValueAttribute) == title)
}

guard AXIsProcessTrusted() else { fputs("accessibility permission is unavailable\n", stderr); exit(3) }
guard CommandLine.arguments.count >= 5, let pid = pid_t(CommandLine.arguments[2]) else { exit(2) }
let command = CommandLine.arguments[1]
let role = CommandLine.arguments[3]
let title = CommandLine.arguments[4]
let occurrence = CommandLine.arguments.count > 5 ? Int(CommandLine.arguments[5]) ?? 0 : 0
let app = AXUIElementCreateApplication(pid)
let found = descendants(app).filter { matches($0, role: role, title: title) }
guard occurrence >= 0 && occurrence < found.count else { fputs("element not found\n", stderr); exit(4) }
let element = found[occurrence]
func postKey(_ key: CGKeyCode, flags: CGEventFlags = []) {
    let source = CGEventSource(stateID: .hidSystemState)
    let down = CGEvent(keyboardEventSource: source, virtualKey: key, keyDown: true)!
    down.flags = flags; down.postToPid(pid)
    let up = CGEvent(keyboardEventSource: source, virtualKey: key, keyDown: false)!
    up.flags = flags; up.postToPid(pid)
}
func postText(_ value: String) {
    let characters = Array(value.utf16)
    let source = CGEventSource(stateID: .hidSystemState)
    let down = CGEvent(keyboardEventSource: source, virtualKey: 0, keyDown: true)!
    down.keyboardSetUnicodeString(stringLength: characters.count, unicodeString: characters); down.postToPid(pid)
    let up = CGEvent(keyboardEventSource: source, virtualKey: 0, keyDown: false)!
    up.keyboardSetUnicodeString(stringLength: characters.count, unicodeString: characters); up.postToPid(pid)
}
if command == "actions" {
    var names: CFArray?
    guard AXUIElementCopyActionNames(element, &names) == .success else { exit(5) }
    print(names ?? [] as CFArray)
} else if command == "press" {
    guard AXUIElementPerformAction(element, kAXPressAction as CFString) == .success else { exit(5) }
} else if command == "open" {
    guard AXUIElementPerformAction(element, "AXOpen" as CFString) == .success else { exit(5) }
} else if command == "confirm" {
    guard AXUIElementPerformAction(element, "AXConfirm" as CFString) == .success else { exit(5) }
} else if command == "select" {
    guard AXUIElementSetAttributeValue(element, kAXSelectedAttribute as CFString, kCFBooleanTrue) == .success else { exit(6) }
} else if command == "set" {
    guard CommandLine.arguments.count > 6 else { exit(2) }
    guard AXUIElementSetAttributeValue(element, kAXValueAttribute as CFString, CommandLine.arguments[6] as CFTypeRef) == .success else { exit(6) }
} else if command == "type" {
    guard CommandLine.arguments.count > 6 else { exit(2) }
    guard AXUIElementSetAttributeValue(element, kAXFocusedAttribute as CFString, kCFBooleanTrue) == .success else { exit(6) }
    guard ["1", "true"].contains(text(element, kAXFocusedAttribute)) else { exit(6) }
    postKey(0, flags: .maskCommand)
    usleep(50_000)
    postText(CommandLine.arguments[6])
} else if command == "openpath" {
    guard CommandLine.arguments.count > 6 else { exit(2) }
    postKey(5, flags: [.maskCommand, .maskShift])
    usleep(300_000)
    postText(CommandLine.arguments[6])
    usleep(100_000)
    postKey(36)
    usleep(300_000)
    postKey(36)
} else if command == "escape" {
    guard AXUIElementSetAttributeValue(element, kAXFocusedAttribute as CFString, kCFBooleanTrue) == .success else { exit(6) }
    guard ["1", "true"].contains(text(element, kAXFocusedAttribute)) else { exit(6) }
    postKey(53)
} else if command == "get" {
    FileHandle.standardOutput.write(Data(text(element, kAXValueAttribute).utf8))
} else if command == "enabled" {
    print(text(element, kAXEnabledAttribute))
} else if command == "count" {
    print(found.count)
} else { exit(2) }
