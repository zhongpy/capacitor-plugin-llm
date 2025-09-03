import Foundation

@objc public class LLM: NSObject {
    @objc public func echo(_ value: String) -> String {
        print(value)
        return value
    }
}
