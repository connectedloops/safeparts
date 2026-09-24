import ApplicationServices
import CoreGraphics
import Foundation

private let maximumElements = 4
private let attributes = [kAXWindowsAttribute, kAXMainWindowAttribute, kAXFocusedWindowAttribute, kAXChildrenAttribute]

struct ElementRecord: Codable {
    let index: Int
    let cfTypeId: UInt
    let pidError: Int32
    let ownerPidMatches: Bool
    let roleError: Int32?
    let role: String?
    let equalsRetainedRoot: Bool
    let equalsFreshRoot: Bool
    let pairwiseEqualIndices: [Int]
    let children: AttributeRecord?
}

struct AttributeRecord: Codable {
    let attribute: String
    let error: Int32
    let cfTypeId: UInt?
    let returnedCount: Int?
    let truncated: Bool
    let elements: [ElementRecord]
}

struct NativeWindow: Codable {
    let ownerPid: Int32
    let layer: Int?
    let bounds: [String: Double]?
}

struct NativeInventory: Codable {
    let semantics: String
    let available: Bool
    let windows: [NativeWindow]
}

struct Sample: Codable {
    let index: Int
    let elapsedMilliseconds: Int
    let retainedRootPidError: Int32
    let freshRootPidError: Int32
    let rootsEqual: Bool
    let nativeOnscreen: NativeInventory
    let nativeAll: NativeInventory
    let retained: [AttributeRecord]
    let fresh: [AttributeRecord]
}

struct Report: Codable {
    let targetPid: Int32
    let messagingTimeoutSeconds: Double
    let retainedTimeoutError: Int32
    let sampleOffsetsSeconds: [Int]
    let samples: [Sample]
}

func owner(_ element: AXUIElement, targetPid: pid_t) -> (AXError, Bool) {
    var pid: pid_t = 0
    let error = AXUIElementGetPid(element, &pid)
    return (error, error == .success && pid == targetPid)
}

func exactRole(_ element: AXUIElement) -> (AXError, String?) {
    var value: CFTypeRef?
    let error = AXUIElementCopyAttributeValue(element, kAXRoleAttribute as CFString, &value)
    guard error == .success, let value, CFGetTypeID(value) == CFStringGetTypeID() else { return (error, nil) }
    return (error, value as? String)
}

func elements(from value: CFTypeRef) -> [AXUIElement]? {
    if CFGetTypeID(value) == AXUIElementGetTypeID() {
        return [unsafeBitCast(value, to: AXUIElement.self)]
    }
    guard CFGetTypeID(value) == CFArrayGetTypeID() else { return nil }
    let array = unsafeBitCast(value, to: CFArray.self)
    var result: [AXUIElement] = []
    for index in 0..<min(CFArrayGetCount(array), maximumElements) {
        let raw = CFArrayGetValueAtIndex(array, index)
        let candidate = unsafeBitCast(raw, to: CFTypeRef.self)
        guard CFGetTypeID(candidate) == AXUIElementGetTypeID() else { continue }
        result.append(unsafeBitCast(candidate, to: AXUIElement.self))
    }
    return result
}

func readAttribute(_ root: AXUIElement, name: String, retained: AXUIElement, fresh: AXUIElement,
                   targetPid: pid_t, allowWindowChildren: Bool) -> AttributeRecord {
    var value: CFTypeRef?
    let error = AXUIElementCopyAttributeValue(root, name as CFString, &value)
    guard error == .success, let value else {
        return AttributeRecord(attribute: name, error: error.rawValue, cfTypeId: nil,
                               returnedCount: nil, truncated: false, elements: [])
    }
    let type = CFGetTypeID(value)
    guard let returned = elements(from: value) else {
        return AttributeRecord(attribute: name, error: error.rawValue, cfTypeId: type,
                               returnedCount: nil, truncated: false, elements: [])
    }
    let fullCount = type == CFArrayGetTypeID() ? CFArrayGetCount(unsafeBitCast(value, to: CFArray.self)) : 1
    let limited = Array(returned.prefix(maximumElements))
    var records: [ElementRecord] = []
    for (index, element) in limited.enumerated() {
        let pid = owner(element, targetPid: targetPid) // owner is deliberately read first
        var roleError: AXError? = nil
        var role: String? = nil
        if pid.1 {
            (roleError, role) = exactRole(element)
        }
        let distinctWindow = allowWindowChildren && pid.1 && role == kAXWindowRole && !CFEqual(element, retained) && !CFEqual(element, fresh)
        let childRecord = distinctWindow ? readAttribute(element, name: kAXChildrenAttribute, retained: retained,
                                                         fresh: fresh, targetPid: targetPid,
                                                         allowWindowChildren: false) : nil
        let equals = limited.enumerated().compactMap { otherIndex, other in
            otherIndex < index && CFEqual(element, other) ? otherIndex : nil
        }
        records.append(ElementRecord(index: index, cfTypeId: CFGetTypeID(element),
                                     pidError: pid.0.rawValue, ownerPidMatches: pid.1,
                                     roleError: roleError?.rawValue, role: role,
                                     equalsRetainedRoot: CFEqual(element, retained),
                                     equalsFreshRoot: CFEqual(element, fresh),
                                     pairwiseEqualIndices: equals, children: childRecord))
    }
    return AttributeRecord(attribute: name, error: error.rawValue, cfTypeId: type,
                           returnedCount: fullCount, truncated: fullCount > maximumElements, elements: records)
}

func inventory(_ option: CGWindowListOption, semantics: String, targetPid: pid_t) -> NativeInventory {
    guard let raw = CGWindowListCopyWindowInfo(option, kCGNullWindowID) as? [[CFString: Any]] else {
        return NativeInventory(semantics: semantics, available: false, windows: [])
    }
    let filtered = raw.compactMap { row -> NativeWindow? in
        guard let pid = row[kCGWindowOwnerPID] as? NSNumber, pid.int32Value == targetPid else { return nil }
        let layer = (row[kCGWindowLayer] as? NSNumber)?.intValue
        var bounds: CGRect = .zero
        let boundsValue = row[kCGWindowBounds]
        let boundsDictionary: CFDictionary? = boundsValue.map { $0 as CFTypeRef }.flatMap {
            CFGetTypeID($0) == CFDictionaryGetTypeID() ? unsafeBitCast($0, to: CFDictionary.self) : nil
        }
        let exactBounds = boundsDictionary != nil && CGRectMakeWithDictionaryRepresentation(boundsDictionary!, &bounds)
            ? ["x": Double(bounds.origin.x), "y": Double(bounds.origin.y), "width": Double(bounds.size.width), "height": Double(bounds.size.height)] : nil
        return NativeWindow(ownerPid: pid.int32Value, layer: layer, bounds: exactBounds)
    }
    return NativeInventory(semantics: semantics, available: true, windows: filtered)
}

guard CommandLine.arguments.count == 2, let targetPid = pid_t(CommandLine.arguments[1]) else { exit(2) }
// Do not request trust or trigger a permission prompt. AX calls report their own errors.
let retained = AXUIElementCreateApplication(targetPid)
let retainedTimeout = AXUIElementSetMessagingTimeout(retained, 1.0)
let start = DispatchTime.now().uptimeNanoseconds
let offsets = [1, 3, 5]
var samples: [Sample] = []
for (index, offset) in offsets.enumerated() {
    let target = start + UInt64(offset) * 1_000_000_000
    let now = DispatchTime.now().uptimeNanoseconds
    if now < target { Thread.sleep(forTimeInterval: Double(target - now) / 1_000_000_000) }
    let fresh = AXUIElementCreateApplication(targetPid)
    _ = AXUIElementSetMessagingTimeout(fresh, 1.0)
    let retainedPid = owner(retained, targetPid: targetPid).0
    let freshPid = owner(fresh, targetPid: targetPid).0
    let retainedRecords = attributes.map { readAttribute(retained, name: $0, retained: retained, fresh: fresh,
                                                         targetPid: targetPid, allowWindowChildren: true) }
    let freshRecords = attributes.map { readAttribute(fresh, name: $0, retained: retained, fresh: fresh,
                                                      targetPid: targetPid, allowWindowChildren: true) }
    samples.append(Sample(index: index, elapsedMilliseconds: Int((DispatchTime.now().uptimeNanoseconds - start) / 1_000_000),
                          retainedRootPidError: retainedPid.rawValue, freshRootPidError: freshPid.rawValue,
                          rootsEqual: CFEqual(retained, fresh),
                          nativeOnscreen: inventory(.optionOnScreenOnly, semantics: "on-screen windows only", targetPid: targetPid),
                          nativeAll: inventory(.optionAll, semantics: "all windows", targetPid: targetPid),
                          retained: retainedRecords, fresh: freshRecords))
}
let report = Report(targetPid: targetPid, messagingTimeoutSeconds: 1.0,
                    retainedTimeoutError: retainedTimeout.rawValue, sampleOffsetsSeconds: offsets, samples: samples)
let encoder = JSONEncoder(); encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
FileHandle.standardOutput.write(try encoder.encode(report)); FileHandle.standardOutput.write(Data("\n".utf8))
