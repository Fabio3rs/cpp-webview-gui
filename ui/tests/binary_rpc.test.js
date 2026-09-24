import test from 'node:test'
import assert from 'node:assert/strict'
import { WireReader, WireWriter, echoBytesBinary, getCounterBinary, methodId, installBinaryBindings, installBinaryEventReceiver } from '../src/binary_rpc.js'
import { decodeCbor, encodeCbor } from '../src/cbor.js'
import { readBootstrap, readNativeEvent, readOpaque, readOutsideDrop, readWindowList, writeBootstrap, writeOpaque } from '../src/native_wire_types.js'
import { nativeBindingNames } from '../src/native_binding_names.js'
import { readFileSync } from 'node:fs'

const rpcConfig = { endpoint: 'app-rpc://native/' }
const successResponse = bytes => {
    const envelope = new Uint8Array(bytes.byteLength + 1)
    envelope.set(bytes, 1)
    return new Response(envelope)
}
const errorResponse = (code, message) =>
    new Response(new WireWriter().u8(1).u32(code).string(message).finish())

test('binary binding names cover the generated C++ registry', () => {
    const path = new URL('../src/generated/native-bindings.json', import.meta.url)
    const index = JSON.parse(readFileSync(path, 'utf8'))
    assert.deepEqual(nativeBindingNames, Object.keys(index).sort())
})

test('every application binding has a direct wire codec', () => {
    const originalWindow = globalThis.window
    try {
        globalThis.window = { __APP_BINARY_RPC__: rpcConfig }
        assert.doesNotThrow(() => installBinaryBindings(nativeBindingNames))
        for (const name of nativeBindingNames) {
            assert.equal(typeof window[name], 'function')
        }
        assert.throws(() => installBinaryBindings(['unmapped']), /Missing binary codec/)
    } finally {
        globalThis.window = originalWindow
    }
})

test('CBOR matches the native wire representation', () => {
    assert.deepEqual(encodeCbor(['olá', 42]),
        Uint8Array.of(0x82, 0x64, 0x6f, 0x6c, 0xc3, 0xa1, 0x18, 0x2a))
    const nativeReply = Uint8Array.of(
        0xa2, 0x64, 0x64, 0x61, 0x74, 0x61, 0xa1,
        0x65, 0x76, 0x61, 0x6c, 0x75, 0x65, 0x18, 0x2a,
        0x62, 0x6f, 0x6b, 0xf5)
    assert.deepEqual(decodeCbor(nativeReply), { data: { value: 42 }, ok: true })
    assert.equal(methodId('ping'), 0x165df089)
})

test('CBOR carries nested values and raw bytes', () => {
    const value = {
        ok: true,
        data: { message: 'olá', values: [null, -123, 3.5], bytes: Uint8Array.of(0, 255) }
    }
    assert.deepEqual(decodeCbor(encodeCbor(value)), value)
})

test('config binding uses typed wire values', async () => {
    const originalFetch = globalThis.fetch
    const originalWindow = globalThis.window
    try {
        globalThis.window = {
            __APP_BINARY_RPC__: rpcConfig,
            getConfig: () => { throw new Error('legacy binding called') }
        }
        globalThis.fetch = async (url, options) => {
            assert.equal(url, `app-rpc://native/${methodId('getConfig')}`)
            assert.equal(options.body.byteLength, 0)
            return successResponse(new WireWriter().string('dark').string('pt-br').finish())
        }
        installBinaryBindings(['getConfig'])
        assert.deepEqual(await window.getConfig(),
            { ok: true, data: { theme: 'dark', lang: 'pt-br' } })
    } finally {
        globalThis.fetch = originalFetch
        globalThis.window = originalWindow
    }
})

test('bootstrap keeps typed window settings and opaque Dockview state', () => {
    const bootstrap = {
        title: 'Inspector', width: 840, left: 17,
        panels: [{ component: 'InspectorPanel', params: { nested: [1, true] } }],
        activePanelId: 'inspector', kind: 'dockview'
    }
    const writer = new WireWriter()
    writeBootstrap(writer, bootstrap)
    const reader = new WireReader(writer.finish())
    assert.deepEqual(readBootstrap(reader), bootstrap)
    reader.finish()
})

test('opaque event, window list and external drop preserve their shapes', () => {
    const event = { type: 'dock.move', payload: { panels: [{ id: 'one' }] } }
    const request = new WireWriter().string('w1')
    writeOpaque(request, event)
    const input = new WireReader(request.finish())
    assert.equal(input.string(), 'w1')
    assert.deepEqual(readOpaque(input), event)
    input.finish()

    const windows = new WireWriter()
        .vector([{ id: 'main', title: 'Main' }],
            (output, item) => output.string(item.id).string(item.title))
    const listReader = new WireReader(windows.finish())
    assert.deepEqual(readWindowList(listReader), [{ id: 'main', title: 'Main' }])
    listReader.finish()

    const outside = new WireWriter().optional(true, output => {
        writeOpaque(output, { panels: [{ id: 'one' }] })
        output.optional({ x: 12.5, y: 7.5 }, (writer, drop) =>
            writer.f64(drop.x).f64(drop.y))
    })
    const outsideReader = new WireReader(outside.finish())
    assert.deepEqual(readOutsideDrop(outsideReader), {
        payload: { panels: [{ id: 'one' }] }, drop: { x: 12.5, y: 7.5 }
    })
    outsideReader.finish()
})

test('typed handler error retains legacy ok/error result', async () => {
    const originalFetch = globalThis.fetch
    const originalWindow = globalThis.window
    try {
        globalThis.window = {
            __APP_BINARY_RPC__: rpcConfig,
            getBootstrap: () => { throw new Error('legacy binding called') }
        }
        globalThis.fetch = async () => errorResponse(400, 'Bootstrap not found')
        installBinaryBindings(['getBootstrap'])
        assert.deepEqual(await window.getBootstrap('missing'), {
            ok: false, error: { code: 400, message: 'Bootstrap not found' }
        })
    } finally {
        globalThis.fetch = originalFetch
        globalThis.window = originalWindow
    }
})

test('binary response rejects unknown status and trailing error bytes', async () => {
    const originalFetch = globalThis.fetch
    const originalWindow = globalThis.window
    try {
        globalThis.window = { __APP_BINARY_RPC__: rpcConfig }
        globalThis.fetch = async () => new Response(Uint8Array.of(2))
        await assert.rejects(getCounterBinary(), /Invalid binary response status/)
        globalThis.fetch = async () => {
            const error = new WireWriter().u8(1).u32(400).string('bad').u8(7)
            return new Response(error.finish())
        }
        await assert.rejects(getCounterBinary(), /Trailing binary response bytes/)
    } finally {
        globalThis.fetch = originalFetch
        globalThis.window = originalWindow
    }
})

test('binary endpoint comes from the page configuration', async () => {
    const originalFetch = globalThis.fetch
    const originalWindow = globalThis.window
    try {
        globalThis.window = {
            __APP_BINARY_RPC__: { endpoint: 'https://app.invalid/rpc/' }
        }
        globalThis.fetch = async url => {
            assert.equal(url, `https://app.invalid/rpc/${methodId('getCounter')}`)
            return successResponse(new WireWriter().i32(42).finish())
        }
        assert.equal(await getCounterBinary(), 42)
    } finally {
        globalThis.fetch = originalFetch
        globalThis.window = originalWindow
    }
})

test('native events cross the scheme as bytes and preserve order', async () => {
    const originalFetch = globalThis.fetch
    const originalWindow = globalThis.window
    const originalCustomEvent = globalThis.CustomEvent
    try {
        const received = []
        globalThis.CustomEvent = class {
            constructor(type, options) {
                this.type = type
                this.detail = options.detail
            }
        }
        globalThis.window = {
            __APP_BINARY_RPC__: rpcConfig,
            dispatchEvent: event => received.push(event.detail)
        }
        globalThis.fetch = async (url, options) => {
            assert.equal(options.method, 'GET')
            const token = Number(url.split('/').at(-1))
            return new Response(new WireWriter().u8(0)
                .bytes(encodeCbor({ type: 'dock.move', payload: { token } })).finish())
        }
        installBinaryEventReceiver()
        await Promise.all([window.__APP_NATIVE_EVENT__(7), window.__APP_NATIVE_EVENT__(8)])
        assert.deepEqual(received, [
            { type: 'dock.move', payload: { token: 7 } },
            { type: 'dock.move', payload: { token: 8 } }
        ])
    } finally {
        globalThis.fetch = originalFetch
        globalThis.window = originalWindow
        globalThis.CustomEvent = originalCustomEvent
    }
})

test('native event tags decode typed control events and opaque drag data', () => {
    const cases = [
        [new WireWriter().u8(1).string('w1'),
            { type: 'native-window.closed', windowId: 'w1' }],
        [new WireWriter().u8(2).string('w1').string('failed'),
            { type: 'native-window.error', windowId: 'w1', message: 'failed' }],
        [new WireWriter().u8(3).string('main'),
            { type: 'dock.dragLeave', payload: { originWindowId: 'main' } }],
        [new WireWriter().u8(4).string('main'),
            { type: 'dock.dragHover', payload: { originWindowId: 'main' } }],
        [new WireWriter().u8(5).string('main').string('w1')
            .bytes(encodeCbor({ panels: [1] })),
        { type: 'dock.dragComplete', payload: {
            originWindowId: 'main', targetWindowId: 'w1', dragPayload: { panels: [1] }
        } }]
    ]
    for (const [writer, expected] of cases) {
        const reader = new WireReader(writer.finish())
        assert.deepEqual(readNativeEvent(reader), expected)
        reader.finish()
    }
    assert.throws(() => readNativeEvent(new WireReader(Uint8Array.of(255))),
        /Unknown native event kind/)
})

test('ping uses typed optional input and struct response', async () => {
    const originalFetch = globalThis.fetch
    const originalWindow = globalThis.window
    try {
        globalThis.window = {
            __APP_BINARY_RPC__: rpcConfig,
            ping: () => { throw new Error('legacy binding called') }
        }
        globalThis.fetch = async (url, options) => {
            assert.equal(url, `app-rpc://native/${methodId('ping')}`)
            const input = new WireReader(options.body)
            assert.equal(input.optional(value => value.string()), 'hello')
            input.finish()
            return successResponse(new WireWriter().string('pong').string('hello').finish())
        }
        installBinaryBindings(['ping'])
        assert.deepEqual(await window.ping('hello'),
            { ok: true, data: { message: 'pong', echo: 'hello' } })
    } finally {
        globalThis.fetch = originalFetch
        globalThis.window = originalWindow
    }
})

test('primitive binding uses its direct wire codec', async () => {
    const originalFetch = globalThis.fetch
    const originalWindow = globalThis.window
    try {
        globalThis.window = {
            __APP_BINARY_RPC__: rpcConfig,
            getCounter: () => { throw new Error('legacy binding called') }
        }
        globalThis.fetch = async (url) => {
            assert.equal(url, `app-rpc://native/${methodId('getCounter')}`)
            return successResponse(new WireWriter().i32(42).finish())
        }
        installBinaryBindings(['getCounter'])
        assert.deepEqual(await window.getCounter(), { ok: true, data: 42 })
    } finally {
        globalThis.fetch = originalFetch
        globalThis.window = originalWindow
    }
})

test('wire values use little endian and UTF-8', () => {
    const bytes = new WireWriter().i32(-123456).string('olá').finish()
    const reader = new WireReader(bytes)
    assert.equal(reader.i32(), -123456)
    assert.equal(reader.string(), 'olá')
    reader.finish()
})

test('wire composes optional strings and vectors of signed integers', () => {
    const bytes = new WireWriter()
        .optional('olá', (writer, value) => writer.string(value))
        .vector([1, -2, 3], (writer, value) => writer.i32(value))
        .optional(null, (writer, value) => writer.string(value))
        .finish()
    const reader = new WireReader(bytes)
    assert.equal(reader.optional(value => value.string()), 'olá')
    assert.deepEqual(reader.vector(value => value.i32()), [1, -2, 3])
    assert.equal(reader.optional(value => value.string()), null)
    reader.finish()
    assert.throws(() => new WireReader(Uint8Array.of(2)).optional(value => value.i32()),
        /Invalid binary optional/)
})

test('reader rejects truncated and trailing bytes', () => {
    assert.throws(() => new WireReader(new Uint8Array([1])).i32(), /Truncated/)
    assert.throws(() => new WireReader(new Uint8Array([1])).finish(), /Trailing/)
})

test('typed call posts bytes and decodes native response', async () => {
    const originalFetch = globalThis.fetch
    const originalWindow = globalThis.window
    try {
        globalThis.window = { __APP_BINARY_RPC__: rpcConfig }
        globalThis.fetch = async (url, options) => {
            assert.equal(url, `app-rpc://native/${methodId('getCounter')}`)
            assert.equal(options.method, 'POST')
            assert.equal(options.body.byteLength, 0)
            return successResponse(new WireWriter().i32(42).finish())
        }
        assert.equal(await getCounterBinary(), 42)
    } finally {
        globalThis.fetch = originalFetch
        globalThis.window = originalWindow
    }
})

test('binary payload retains its bytes', async () => {
    const originalFetch = globalThis.fetch
    const originalWindow = globalThis.window
    try {
        globalThis.window = { __APP_BINARY_RPC__: rpcConfig }
        const source = new Uint8Array([0, 255, 17, 42])
        globalThis.fetch = async (url, options) => {
            assert.equal(url, 'app-rpc://native/5')
            const input = new WireReader(options.body)
            assert.deepEqual(input.bytes(), source)
            input.finish()
            return successResponse(new WireWriter().bytes(source).finish())
        }
        assert.deepEqual(await echoBytesBinary(source), source)
    } finally {
        globalThis.fetch = originalFetch
        globalThis.window = originalWindow
    }
})
