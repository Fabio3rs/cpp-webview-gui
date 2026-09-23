import { decodeCbor, encodeCbor } from './cbor.js'
import {
    readBootstrap, readOpaque, readOutsideDrop, readWindowList,
    writeBootstrap, writeOpaque
} from './native_wire_types.js'

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

    u8(value) {
        if (!Number.isInteger(value) || value < 0 || value > 255) {
            throw new RangeError('Expected uint8')
        }
        return this.append(Uint8Array.of(value))
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

    f64(value) {
        if (typeof value !== 'number' || !Number.isFinite(value)) {
            throw new RangeError('Expected finite double')
        }
        const bytes = new Uint8Array(8)
        new DataView(bytes.buffer).setFloat64(0, value, true)
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

    optional(value, write) {
        this.u8(value === null || value === undefined ? 0 : 1)
        if (value !== null && value !== undefined) write(this, value)
        return this
    }

    vector(values, write) {
        if (values.length > maxMessageSize) {
            throw new RangeError('Binary vector too large')
        }
        this.u32(values.length)
        for (const value of values) write(this, value)
        return this
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

    optional(read) {
        const present = this.u8()
        if (present > 1) throw new Error('Invalid binary optional')
        return present === 1 ? read(this) : null
    }

    vector(read) {
        const count = this.u32()
        if (count > this.bytesView.byteLength - this.offset) {
            throw new Error('Invalid binary vector length')
        }
        const values = []
        for (let index = 0; index < count; index++) values.push(read(this))
        return values
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
        const error = new Error(await response.text())
        error.status = response.status
        throw error
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

export function installBinaryEventReceiver() {
    if (!window.__APP_BINARY_RPC__) return
    let pending = Promise.resolve()
    window.__APP_NATIVE_EVENT__ = token => {
        pending = pending.then(async () => {
            const response = await fetch(`${endpoint}event/${token}`, {
                method: 'GET', cache: 'no-store'
            })
            if (!response.ok) throw new Error(`Native event ${response.status}`)
            const detail = decodeCbor(new Uint8Array(await response.arrayBuffer()))
            window.dispatchEvent(new CustomEvent('native-event', { detail }))
        }).catch(error => {
            console.error('[UI] Failed to receive native event:', error)
        })
        return pending
    }
}

async function callTyped(name, write, read) {
    const writer = new WireWriter()
    write(writer)
    const response = await callBinary(methodId(name), writer.finish())
    const value = read(response)
    response.finish()
    return value
}

const noArgs = () => {}
const noResult = () => ({})

export function installBinaryBindings(names) {
    if (!window.__APP_BINARY_RPC__) return
    const direct = {
        getCounter: getCounterBinary,
        getPi: getPiBinary,
        getStatus: getStatusBinary,
        isReady: isReadyBinary,
        ping: pingBinary,
        getVersion: () => callTyped('getVersion', noArgs,
            response => ({ version: response.string() })),
        openFile: path => callTyped('openFile', request => request.string(path),
            response => ({ path: response.string(), status: response.string() })),
        getConfig: () => callTyped('getConfig', noArgs,
            response => ({ theme: response.string(), lang: response.string() })),
        createNativeWindow: bootstrap => callTyped('createNativeWindow',
            request => writeBootstrap(request, bootstrap), response => response.string()),
        getBootstrap: windowId => callTyped('getBootstrap',
            request => request.string(windowId), readBootstrap),
        postNativeEvent: (windowId, event) => callTyped('postNativeEvent',
            request => { request.string(windowId); writeOpaque(request, event) }, noResult),
        closeNativeWindow: windowId => callTyped('closeNativeWindow',
            request => request.string(windowId), noResult),
        listNativeWindows: () => callTyped('listNativeWindows', noArgs, readWindowList),
        startNativeDrag: (windowId, payload) => callTyped('startNativeDrag',
            request => { request.string(windowId); writeOpaque(request, payload) }, noResult),
        completeNativeDrag: windowId => callTyped('completeNativeDrag',
            request => request.string(windowId), response => response.optional(readOpaque)),
        stopNativeDrag: () => callTyped('stopNativeDrag', noArgs, noResult),
        completeNativeDragOutside: windowId => callTyped('completeNativeDragOutside',
            request => request.string(windowId), readOutsideDrop)
    }
    for (const name of names) {
        if (!direct[name]) {
            throw new Error(`Missing binary codec for ${name}`)
        }
        if (typeof window[name] === 'function') {
            window[name] = async (...args) => {
                try {
                    const data = await direct[name](...args)
                    return data === null ? { ok: true } : { ok: true, data }
                } catch (error) {
                    return { ok: false, error: {
                        code: error.status || 500,
                        message: error.message || String(error)
                    } }
                }
            }
        }
    }
}

export async function getCounterBinary() {
    const response = await callBinary(methodId('getCounter'))
    const value = response.i32()
    response.finish()
    return value
}

export async function getPiBinary() {
    const response = await callBinary(methodId('getPi'))
    const value = response.f64()
    response.finish()
    return value
}

export async function isReadyBinary() {
    const response = await callBinary(methodId('isReady'))
    const value = response.u8()
    if (value !== 0 && value !== 1) {
        throw new Error('Invalid binary boolean')
    }
    response.finish()
    return value === 1
}

export async function getStatusBinary() {
    const response = await callBinary(methodId('getStatus'))
    const value = response.string()
    response.finish()
    return value
}

export async function pingBinary(message) {
    const request = new WireWriter()
        .optional(message, (writer, value) => writer.string(value))
        .finish()
    const response = await callBinary(methodId('ping'), request)
    const result = { message: response.string(), echo: response.string() }
    response.finish()
    return result
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
