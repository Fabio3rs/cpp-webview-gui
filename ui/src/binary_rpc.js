import { decodeCbor, encodeCbor } from './cbor.js'

const endpoint = 'app-rpc://native/'
const textEncoder = new TextEncoder()
const textDecoder = new TextDecoder()
const maxMessageSize = 16 * 1024 * 1024

export class WireWriter {
    constructor() {
        this.parts = []
        this.size = 0
    }

    append(bytes) {
        if (bytes.byteLength > maxMessageSize - this.size) {
            throw new RangeError('Binary message too large')
        }
        this.parts.push(bytes)
        this.size += bytes.byteLength
        return this
    }

    u32(value) {
        if (!Number.isInteger(value) || value < 0 || value > 0xffffffff) {
            throw new RangeError('Expected uint32')
        }
        const bytes = new Uint8Array(4)
        new DataView(bytes.buffer).setUint32(0, value, true)
        return this.append(bytes)
    }

    i32(value) {
        if (!Number.isInteger(value) || value < -0x80000000 || value > 0x7fffffff) {
            throw new RangeError('Expected int32')
        }
        const bytes = new Uint8Array(4)
        new DataView(bytes.buffer).setInt32(0, value, true)
        return this.append(bytes)
    }

    bytes(value) {
        const data = value instanceof Uint8Array ? value : new Uint8Array(value)
        if (data.byteLength > maxMessageSize - 4 - this.size) {
            throw new RangeError('Binary message too large')
        }
        this.u32(data.byteLength)
        return this.append(data)
    }

    string(value) {
        return this.bytes(textEncoder.encode(value))
    }

    finish() {
        const result = new Uint8Array(this.size)
        let offset = 0
        for (const part of this.parts) {
            result.set(part, offset)
            offset += part.byteLength
        }
        return result
    }
}

export class WireReader {
    constructor(bytes) {
        if (bytes.byteLength > maxMessageSize) {
            throw new RangeError('Binary response too large')
        }
        this.bytesView = bytes
        this.view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength)
        this.offset = 0
    }

    require(size) {
        if (size > this.bytesView.byteLength - this.offset) {
            throw new Error('Truncated binary response')
        }
    }

    u8() {
        this.require(1)
        return this.view.getUint8(this.offset++)
    }

    u32() {
        this.require(4)
        const value = this.view.getUint32(this.offset, true)
        this.offset += 4
        return value
    }

    i32() {
        this.require(4)
        const value = this.view.getInt32(this.offset, true)
        this.offset += 4
        return value
    }

    f64() {
        this.require(8)
        const value = this.view.getFloat64(this.offset, true)
        this.offset += 8
        return value
    }

    bytes() {
        const length = this.u32()
        this.require(length)
        const result = this.bytesView.subarray(this.offset, this.offset + length)
        this.offset += length
        return result
    }

    string() {
        return textDecoder.decode(this.bytes())
    }

    finish() {
        if (this.offset !== this.bytesView.byteLength) {
            throw new Error('Trailing binary response bytes')
        }
    }
}

async function requestBinary(id, request) {
    if (!window.__APP_BINARY_RPC__) {
        throw new Error('Binary RPC is unavailable on this backend')
    }
    const response = await fetch(`${endpoint}${id}`, {
        method: 'POST',
        body: request,
        headers: { 'Content-Type': 'application/octet-stream' }
    })
    if (!response.ok) {
        throw new Error(await response.text())
    }
    return new Uint8Array(await response.arrayBuffer())
}

export async function callBinary(id, request = new Uint8Array()) {
    return new WireReader(await requestBinary(id, request))
}

export function methodId(name) {
    let hash = 2166136261
    for (const byte of textEncoder.encode(name)) {
        hash = Math.imul(hash ^ byte, 16777619) >>> 0
    }
    return hash
}

export async function callCbor(name, args) {
    const response = await requestBinary(methodId(name), encodeCbor(args))
    return decodeCbor(response)
}

export function installBinaryBindings(names) {
    if (!window.__APP_BINARY_RPC__) return
    const direct = {
        getCounter: getCounterBinary,
        getPi: getPiBinary,
        getStatus: getStatusBinary,
        isReady: isReadyBinary
    }
    for (const name of names) {
        if (typeof window[name] === 'function') {
            window[name] = direct[name]
                ? async () => ({ ok: true, data: await direct[name]() })
                : (...args) => callCbor(name, args)
        }
    }
}

export async function getCounterBinary() {
    const response = await callBinary(1)
    const value = response.i32()
    response.finish()
    return value
}

export async function getPiBinary() {
    const response = await callBinary(2)
    const value = response.f64()
    response.finish()
    return value
}

export async function isReadyBinary() {
    const response = await callBinary(3)
    const value = response.u8()
    if (value !== 0 && value !== 1) {
        throw new Error('Invalid binary boolean')
    }
    response.finish()
    return value === 1
}

export async function getStatusBinary() {
    const response = await callBinary(4)
    const value = response.string()
    response.finish()
    return value
}

export async function addI32Binary(left, right) {
    const request = new WireWriter().i32(left).i32(right).finish()
    const response = await callBinary(6, request)
    const value = response.i32()
    response.finish()
    return value
}

export async function echoBytesBinary(bytes) {
    const response = await callBinary(5, new WireWriter().bytes(bytes).finish())
    const result = response.bytes()
    response.finish()
    return result
}
