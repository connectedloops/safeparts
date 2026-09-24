import ApplicationServices
import Foundation

let maximumNodes = 4_096
let maximumDepth = 64

enum HarnessError: Error {
    case notFound
    case ax(AXError)
    case graph(String)
    case unsafe(String)
}

enum AttributeRequirement { case required, optional }

func classifyAttributeError(_ error: AXError, requirement: AttributeRequirement) throws -> Bool {
    if error == .success { return true }
    if requirement == .optional && (error == .noValue || error == .attributeUnsupported) { return false }
    throw HarnessError.ax(error)
}

func uniqueMatch<T>(_ values: [T], where predicate: (T) -> Bool) throws -> T {
    let matches = values.filter(predicate)
    guard matches.count == 1 else { throw HarnessError.unsafe("ambiguous") }
    return matches[0]
}

func requireUniform<T>(_ values: [T], equal: (T, T) -> Bool) throws -> T {
    guard let first = values.first, values.allSatisfy({ equal($0, first) }) else {
        throw HarnessError.unsafe("non-uniform")
    }
    return first
}

func requireExpectedOwners<T>(_ values: [T], expected: pid_t, owner: (T) throws -> pid_t) throws {
    guard try values.allSatisfy({ try owner($0) == expected }) else { throw HarnessError.unsafe("foreign-owner") }
}

func boundedWalk<Node>(root: Node, children: (Node) throws -> [Node], equal: (Node, Node) -> Bool,
                       nodeLimit: Int = maximumNodes, depthLimit: Int = maximumDepth) throws -> [Node] {
    var visited: [Node] = []
    func visit(_ node: Node, depth: Int, path: [Node]) throws {
        if path.contains(where: { equal($0, node) }) { throw HarnessError.graph("cycle") }
        if visited.contains(where: { equal($0, node) }) { return }
        guard depth <= depthLimit else { throw HarnessError.graph("depth") }
        guard visited.count < nodeLimit else { throw HarnessError.graph("nodes") }
        visited.append(node)
        let next = try children(node)
        for child in next { try visit(child, depth: depth + 1, path: path + [node]) }
    }
    try visit(root, depth: 0, path: [])
    return visited
}

func boundedAncestor<Node>(start: Node, parent: (Node) throws -> Node?, equal: (Node, Node) -> Bool,
                           accept: (Node) throws -> Bool, depthLimit: Int = maximumDepth) throws -> Node? {
    var current: Node? = start
    var visited: [Node] = []
    var depth = 0
    while let candidate = current {
        if visited.contains(where: { equal($0, candidate) }) { throw HarnessError.graph("ancestor-cycle") }
        guard depth <= depthLimit else { throw HarnessError.graph("ancestor-depth") }
        visited.append(candidate)
        if try accept(candidate) { return candidate }
        current = try parent(candidate)
        depth += 1
    }
    return nil
}

func runOfflineTests() throws {
    let selfCycle = [0: [0]]
    do { _ = try boundedWalk(root: 0, children: { selfCycle[$0] ?? [] }, equal: ==); throw HarnessError.unsafe("self-cycle accepted") }
    catch HarnessError.graph { }
    let twoCycle = [0: [1], 1: [0]]
    do { _ = try boundedWalk(root: 0, children: { twoCycle[$0] ?? [] }, equal: ==); throw HarnessError.unsafe("two-cycle accepted") }
    catch HarnessError.graph { }
    let dag = [0: [1, 2], 1: [3], 2: [3]]
    let dagResult = try boundedWalk(root: 0, children: { dag[$0] ?? [] }, equal: ==)
    guard dagResult == [0, 1, 3, 2] else { throw HarnessError.unsafe("DAG") }
    let deepChildren: (Int) throws -> [Int] = { value in value < 10 ? [value + 1] : [] }
    do { _ = try boundedWalk(root: 0, children: deepChildren, equal: { $0 == $1 }, depthLimit: 3); throw HarnessError.unsafe("depth accepted") }
    catch HarnessError.graph { }
    let broadChildren: (Int) throws -> [Int] = { value in value == 0 ? Array(1...10) : [] }
    do { _ = try boundedWalk(root: 0, children: broadChildren, equal: { $0 == $1 }, nodeLimit: 4); throw HarnessError.unsafe("breadth accepted") }
    catch HarnessError.graph { }
    do { _ = try boundedAncestor(start: 0, parent: { $0 == 0 ? 1 : 0 }, equal: ==, accept: { _ in false }); throw HarnessError.unsafe("ancestor cycle accepted") }
    catch HarnessError.graph { }
    guard try classifyAttributeError(.noValue, requirement: .optional) == false,
          try classifyAttributeError(.attributeUnsupported, requirement: .optional) == false else { throw HarnessError.unsafe("optional") }
    for error in [AXError.cannotComplete, .apiDisabled, .invalidUIElement, .failure] {
        do { _ = try classifyAttributeError(error, requirement: .optional); throw HarnessError.unsafe("AX error accepted") }
        catch HarnessError.ax { }
    }
    do { _ = try uniqueMatch(["same", "same"], where: { $0 == "same" }); throw HarnessError.unsafe("duplicate") }
    catch HarnessError.unsafe { }
    do { _ = try uniqueMatch(["other"], where: { $0 == "missing" }); throw HarnessError.unsafe("missing") }
    catch HarnessError.unsafe { }
    let rows = [("a", 1, pid_t(7)), ("b", 1, pid_t(7))]
    _ = try requireUniform(rows, equal: { $0.1 == $1.1 })
    try requireExpectedOwners(rows, expected: 7, owner: { $0.2 })
    let wrongOutline = [("a", 1), ("b", 2)]
    do { _ = try requireUniform(wrongOutline, equal: { $0.1 == $1.1 }); throw HarnessError.unsafe("outline accepted") }
    catch HarnessError.unsafe { }
    let foreign = [("a", pid_t(7)), ("b", pid_t(8))]
    do { try requireExpectedOwners(foreign, expected: 7, owner: { $0.1 }); throw HarnessError.unsafe("foreign accepted") }
    catch HarnessError.unsafe { }
    print("AX_HARNESS_OFFLINE_OK")
}

func readAttribute(_ element: AXUIElement, _ name: String, _ requirement: AttributeRequirement) throws -> CFTypeRef? {
    var value: CFTypeRef?
    let error = AXUIElementCopyAttributeValue(element, name as CFString, &value)
    guard try classifyAttributeError(error, requirement: requirement) else { return nil }
    guard let value else {
        if requirement == .optional { return nil }
        throw HarnessError.ax(.noValue)
    }
    return value
}

func typedElement(_ value: CFTypeRef) throws -> AXUIElement {
    guard CFGetTypeID(value) == AXUIElementGetTypeID() else { throw HarnessError.unsafe("element-type") }
    return unsafeBitCast(value, to: AXUIElement.self)
}

func requiredText(_ element: AXUIElement, _ name: String) throws -> String {
    guard let value = try readAttribute(element, name, .required) else { throw HarnessError.ax(.noValue) }
    return String(describing: value)
}

func optionalText(_ element: AXUIElement, _ name: String) throws -> String? {
    guard let value = try readAttribute(element, name, .optional) else { return nil }
    return String(describing: value)
}

func owner(_ element: AXUIElement) throws -> pid_t {
    var value: pid_t = 0
    let error = AXUIElementGetPid(element, &value)
    guard error == .success else { throw HarnessError.ax(error) }
    return value
}

func requireOwner(_ element: AXUIElement, pid: pid_t) throws {
    guard try owner(element) == pid else { throw HarnessError.unsafe("foreign-owner") }
}

func children(_ element: AXUIElement) throws -> [AXUIElement] {
    guard let value = try readAttribute(element, kAXChildrenAttribute, .optional) else { return [] }
    guard let result = value as? [AXUIElement] else { throw HarnessError.unsafe("children-type") }
    return result
}

func descendants(_ root: AXUIElement) throws -> [AXUIElement] {
    try boundedWalk(root: root, children: children, equal: { CFEqual($0, $1) })
}

func role(_ element: AXUIElement) throws -> String { try requiredText(element, kAXRoleAttribute) }

func matches(_ element: AXUIElement, role expectedRole: String, title: String) throws -> Bool {
    guard try role(element) == expectedRole else { return false }
    let values = try [kAXTitleAttribute, kAXDescriptionAttribute, kAXValueAttribute].compactMap {
        try optionalText(element, $0)
    }
    return values.contains(title)
}

func ancestor(_ start: AXUIElement, role expectedRole: String) throws -> AXUIElement? {
    try boundedAncestor(start: start, parent: { element in
        guard let value = try readAttribute(element, kAXParentAttribute, .optional) else { return nil }
        return try typedElement(value)
    }, equal: { CFEqual($0, $1) }, accept: { try role($0) == expectedRole })
}

func uniqueDialog(_ app: AXUIElement, title: String, pid: pid_t) throws -> AXUIElement {
    let candidates = try descendants(app).filter {
        let candidateRole = try role($0)
        let candidateTitle = try optionalText($0, kAXTitleAttribute)
        return (candidateRole == kAXWindowRole || candidateRole == kAXSheetRole) && candidateTitle == title
    }
    let dialog = try uniqueMatch(candidates, where: { _ in true })
    try requireOwner(dialog, pid: pid)
    return dialog
}

func uniqueRow(in dialog: AXUIElement, filename: String, pid: pid_t) throws -> (AXUIElement, AXUIElement) {
    let fields = try descendants(dialog).filter { try matches($0, role: kAXTextFieldRole, title: filename) }
    let field = try uniqueMatch(fields, where: { _ in true })
    try requireOwner(field, pid: pid)
    guard let row = try ancestor(field, role: kAXRowRole), let outline = try ancestor(row, role: kAXOutlineRole) else {
        throw HarnessError.unsafe("row-structure")
    }
    try requireOwner(row, pid: pid); try requireOwner(outline, pid: pid)
    return (row, outline)
}

func semanticSaveNameField(in dialog: AXUIElement, pid: pid_t) throws -> AXUIElement {
    let fields = try descendants(dialog).filter { try role($0) == kAXTextFieldRole }
    let semantic = try fields.filter { field in
        let identifier = try optionalText(field, kAXIdentifierAttribute)
        let title = try optionalText(field, kAXTitleAttribute)
        let description = try optionalText(field, kAXDescriptionAttribute)
        if identifier == "saveAsNameTextField" || title == "Save As:" || description == "Save As:" { return true }
        if let titleValue = try readAttribute(field, kAXTitleUIElementAttribute, .optional) {
            let titleElement = try typedElement(titleValue)
            try requireOwner(titleElement, pid: pid)
            return try optionalText(titleElement, kAXTitleAttribute) == "Save As:"
        }
        return false
    }
    let field = try uniqueMatch(semantic, where: { _ in true })
    try requireOwner(field, pid: pid)
    return field
}

func perform(_ element: AXUIElement, _ action: String) throws {
    let error = AXUIElementPerformAction(element, action as CFString)
    guard error == .success else { throw HarnessError.ax(error) }
}

func setValue(_ element: AXUIElement, _ attribute: String, _ value: CFTypeRef) throws {
    let error = AXUIElementSetAttributeValue(element, attribute as CFString, value)
    guard error == .success else { throw HarnessError.ax(error) }
}

func postKey(_ pid: pid_t, _ key: CGKeyCode, flags: CGEventFlags = []) {
    let source = CGEventSource(stateID: .hidSystemState)
    let down = CGEvent(keyboardEventSource: source, virtualKey: key, keyDown: true)!
    down.flags = flags; down.postToPid(pid)
    let up = CGEvent(keyboardEventSource: source, virtualKey: key, keyDown: false)!
    up.flags = flags; up.postToPid(pid)
}

func postText(_ pid: pid_t, _ value: String) {
    let characters = Array(value.utf16)
    let source = CGEventSource(stateID: .hidSystemState)
    let down = CGEvent(keyboardEventSource: source, virtualKey: 0, keyDown: true)!
    down.keyboardSetUnicodeString(stringLength: characters.count, unicodeString: characters); down.postToPid(pid)
    let up = CGEvent(keyboardEventSource: source, virtualKey: 0, keyDown: false)!
    up.keyboardSetUnicodeString(stringLength: characters.count, unicodeString: characters); up.postToPid(pid)
}

func runHarness() throws {
    if CommandLine.arguments.count == 2 && CommandLine.arguments[1] == "--self-test" { try runOfflineTests(); return }
    guard AXIsProcessTrusted() else { fputs("accessibility-unavailable\n", stderr); exit(3) }
    guard CommandLine.arguments.count >= 5, let pid = pid_t(CommandLine.arguments[2]) else { exit(2) }
    let command = CommandLine.arguments[1]
    let expectedRole = CommandLine.arguments[3]
    let title = CommandLine.arguments[4]
    let occurrence = CommandLine.arguments.count > 5 ? Int(CommandLine.arguments[5]) ?? 0 : 0
    let app = AXUIElementCreateApplication(pid)
    let timeoutError = AXUIElementSetMessagingTimeout(app, 2.0)
    guard timeoutError == .success else { throw HarnessError.ax(timeoutError) }
    try requireOwner(app, pid: pid)

    if command == "dialog-selectrow" || command == "dialog-selectrows" || command == "dialog-set-save-name" {
        guard CommandLine.arguments.count > 6 else { exit(2) }
        let dialog = try uniqueDialog(app, title: title, pid: pid)
        if command == "dialog-set-save-name" {
            let field = try semanticSaveNameField(in: dialog, pid: pid)
            try setValue(field, kAXValueAttribute, CommandLine.arguments[6] as CFTypeRef)
            return
        }
        let names = CommandLine.arguments[6].split(separator: "\n").map(String.init)
        let resolved = try names.map { try uniqueRow(in: dialog, filename: $0, pid: pid) }
        try requireExpectedOwners(resolved, expected: pid, owner: { try owner($0.0) })
        let firstOutline = try requireUniform(resolved.map(\.1), equal: { CFEqual($0, $1) })
        if command == "dialog-selectrow" {
            try setValue(resolved[0].0, kAXSelectedAttribute, kCFBooleanTrue)
        } else {
            try setValue(firstOutline, kAXSelectedRowsAttribute, resolved.map(\.0) as CFArray)
        }
        return
    }

    let found = try descendants(app).filter { try matches($0, role: expectedRole, title: title) }
    guard occurrence >= 0 && occurrence < found.count else { throw HarnessError.notFound }
    let element = found[occurrence]
    try requireOwner(element, pid: pid)
    switch command {
    case "actions":
        var names: CFArray?
        let error = AXUIElementCopyActionNames(element, &names)
        guard error == .success else { throw HarnessError.ax(error) }
        print(names ?? [] as CFArray)
    case "press": try perform(element, kAXPressAction)
    case "open": try perform(element, "AXOpen")
    case "confirm": try perform(element, "AXConfirm")
    case "select": try setValue(element, kAXSelectedAttribute, kCFBooleanTrue)
    case "set":
        guard CommandLine.arguments.count > 6 else { exit(2) }
        try setValue(element, kAXValueAttribute, CommandLine.arguments[6] as CFTypeRef)
    case "type":
        guard CommandLine.arguments.count > 6 else { exit(2) }
        try setValue(element, kAXFocusedAttribute, kCFBooleanTrue)
        guard ["1", "true"].contains(try requiredText(element, kAXFocusedAttribute)) else { throw HarnessError.unsafe("focus") }
        postKey(pid, 0, flags: .maskCommand); usleep(50_000); postText(pid, CommandLine.arguments[6])
    case "escape":
        try perform(element, kAXRaiseAction)
        guard let focusedValue = try readAttribute(app, kAXFocusedWindowAttribute, .required) else { throw HarnessError.ax(.noValue) }
        let focused = try typedElement(focusedValue)
        guard CFEqual(focused, element) else { throw HarnessError.unsafe("focused-window") }
        postKey(pid, 53)
    case "get":
        let value = try requiredText(element, kAXValueAttribute)
        FileHandle.standardOutput.write(Data(value.utf8))
    case "enabled": print(try requiredText(element, kAXEnabledAttribute))
    case "count": print(found.count)
    default: exit(2)
    }
}

do {
    try runHarness()
} catch HarnessError.notFound {
    fputs("ordinary-element-absent\n", stderr); exit(4)
} catch HarnessError.ax(let error) {
    fputs("ax-error:\(error.rawValue)\n", stderr); exit(8)
} catch HarnessError.graph(let reason) {
    fputs("ax-graph:\(reason)\n", stderr); exit(9)
} catch HarnessError.unsafe(let reason) {
    fputs("ax-unsafe:\(reason)\n", stderr); exit(10)
} catch {
    fputs("helper-internal-error\n", stderr); exit(11)
}
