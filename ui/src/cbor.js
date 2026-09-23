const encoder = new TextEncoder()
const decoder = new TextDecoder()
const maxBytes = 16 * 1024 * 1024
const maxDepth = 64

export function encodeCbor(value) {
    const parts = []
    let size = 0
    const append = bytes => {
        if (bytes.byteLength > maxBytes - size) throw new RangeError('CBOR too large')
        parts.push(bytes)
        size += bytes.byteLength
    }
    const head = (major, length) => {
        if (length < 24) {
            append(Uint8Array.of((major << 5) | length))
        } else if (length <= 0xff) {
            append(Uint8Array.of((major << 5) | 24, length))
        } else if (length <= 0xffff) {
            const bytes = new Uint8Array(3)
            bytes[0] = (major << 5) | 25
            new DataView(bytes.buffer).setUint16(1, length)
            append(bytes)
        } else if (length <= 0xffffffff) {
            const bytes = new Uint8Array(5)
            bytes[0] = (major << 5) | 26
            new DataView(bytes.buffer).setUint32(1, length)
            append(bytes)
        } else if (Number.isSafeInteger(length) && length >= 0) {
            const bytes = new Uint8Array(9)
            bytes[0] = (major << 5) | 27
            new DataView(bytes.buffer).setBigUint64(1, BigInt(length))
            append(bytes)
        } else {
            throw new RangeError('CBOR integer outside safe range')
        }
    }
    const visit = (item, depth) => {
        if (depth > maxDepth) throw new RangeError('CBOR nesting too deep')
        if (item === null || item === undefined) {
            append(Uint8Array.of(0xf6))
        } else if (item === false || item === true) {
            append(Uint8Array.of(item ? 0xf5 : 0xf4))
        } else if (typeof item === 'number') {
            if (!Number.isFinite(item)) {
                append(Uint8Array.of(0xf6))
            } else if (Number.isSafeInteger(item)) {
                item >= 0 ? head(0, item) : head(1, -1 - item)
            } else {
                const bytes = new Uint8Array(9)
                bytes[0] = 0xfb
                new DataView(bytes.buffer).setFloat64(1, item)
                append(bytes)
            }
        } else if (typeof item === 'string') {
            const bytes = encoder.encode(item)
            head(3, bytes.byteLength)
            append(bytes)
        } else if (item instanceof Uint8Array || item instanceof ArrayBuffer) {
            const bytes = item instanceof Uint8Array ? item : new Uint8Array(item)
            head(2, bytes.byteLength)
            append(bytes)
        } else if (Array.isArray(item)) {
            head(4, item.length)
            for (const element of item) visit(element, depth + 1)
        } else if (typeof item?.toJSON === 'function') {
            visit(item.toJSON(), depth + 1)
        } else if (typeof item === 'object') {
            const keys = Object.keys(item).filter(key => item[key] !== undefined)
            head(5, keys.length)
            for (const key of keys) {
                visit(key, depth + 1)
                visit(item[key], depth + 1)
            }
        } else {
            throw new TypeError('Unsupported CBOR value')
        }
    }
    visit(value, 0)
    const output = new Uint8Array(size)
    let offset = 0
    for (const part of parts) {
        output.set(part, offset)
        offset += part.byteLength
    }
    return output
}

export function decodeCbor(bytes) {
    if (bytes.byteLength > maxBytes) throw new RangeError('CBOR too large')
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength)
    let offset = 0
    const requireBytes = count => {
        if (count > bytes.byteLength - offset) throw new Error('Truncated CBOR')
    }
    const readLength = info => {
        if (info < 24) return info
        if (info === 24) {
            requireBytes(1)
            return view.getUint8(offset++)
        }
        if (info === 25) {
            requireBytes(2)
            const value = view.getUint16(offset)
            offset += 2
            return value
        }
        if (info === 26) {
            requireBytes(4)
            const value = view.getUint32(offset)
            offset += 4
            return value
        }
        if (info === 27) {
            requireBytes(8)
            const value = Number(view.getBigUint64(offset))
            offset += 8
            if (!Number.isSafeInteger(value)) throw new RangeError('Unsafe CBOR integer')
            return value
        }
        throw new Error('Unsupported CBOR length')
    }
    const visit = depth => {
        if (depth > maxDepth) throw new RangeError('CBOR nesting too deep')
        requireBytes(1)
        const first = view.getUint8(offset++)
        const major = first >> 5
        const info = first & 31
        if (major === 7) {
            if (info === 20) return false
            if (info === 21) return true
            if (info === 22 || info === 23) return null
            if (info === 26) {
                requireBytes(4)
                const value = view.getFloat32(offset)
                offset += 4
                return value
            }
            if (info === 27) {
                requireBytes(8)
                const value = view.getFloat64(offset)
                offset += 8
                return value
            }
            throw new Error('Unsupported CBOR simple value')
        }
        const length = readLength(info)
        if (major === 0) return length
        if (major === 1) return -1 - length
        if (major === 2 || major === 3) {
            requireBytes(length)
            const part = bytes.subarray(offset, offset + length)
            offset += length
            return major === 2 ? part : decoder.decode(part)
        }
        if (major === 4) {
            if (length > bytes.byteLength - offset) throw new Error('Invalid CBOR array')
            const result = []
            for (let index = 0; index < length; index++) result.push(visit(depth + 1))
            return result
        }
        if (major === 5) {
            if (length > (bytes.byteLength - offset) / 2) throw new Error('Invalid CBOR map')
            const entries = []
            for (let index = 0; index < length; index++) {
                const key = visit(depth + 1)
                if (typeof key !== 'string') throw new Error('Non-string CBOR map key')
                entries.push([key, visit(depth + 1)])
            }
            return Object.fromEntries(entries)
        }
        throw new Error('Unsupported CBOR major type')
    }
    const result = visit(0)
    if (offset !== bytes.byteLength) throw new Error('Trailing CBOR bytes')
    return result
}
