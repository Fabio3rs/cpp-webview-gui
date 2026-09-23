import test from 'node:test'
import assert from 'node:assert/strict'
import { WireReader, WireWriter, echoBytesBinary, getCounterBinary, methodId, installBinaryBindings } from '../src/binary_rpc.js'
import { decodeCbor, encodeCbor } from '../src/cbor.js'
import { nativeBindingNames } from '../src/native_binding_names.js'
import { readFileSync } from 'node:fs'

test('binary binding names cover the generated C++ registry', () => {
    const path = new URL('../src/generated/native-bindings.json', import.meta.url)
    const index = JSON.parse(readFileSync(path, 'utf8'))
    assert.deepEqual(nativeBindingNames, Object.keys(index).sort())
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

test('named binding uses CBOR in binary mode', async () => {
    const originalFetch = globalThis.fetch
    const originalWindow = globalThis.window
    try {
        globalThis.window = {
            __APP_BINARY_RPC__: true,
            ping: () => { throw new Error('legacy binding called') }
        }
        globalThis.fetch = async (url, options) => {
            assert.equal(url, `app-rpc://native/${methodId('ping')}`)
            assert.deepEqual(decodeCbor(options.body), ['hello'])
            return new Response(encodeCbor({ ok: true, data: { message: 'pong' } }))
        }
        installBinaryBindings(['ping'])
        assert.deepEqual(await window.ping('hello'),
            { ok: true, data: { message: 'pong' } })
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
            __APP_BINARY_RPC__: true,
            getCounter: () => { throw new Error('legacy binding called') }
        }
        globalThis.fetch = async (url) => {
            assert.equal(url, 'app-rpc://native/1')
            return new Response(new WireWriter().i32(42).finish())
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

test('reader rejects truncated and trailing bytes', () => {
    assert.throws(() => new WireReader(new Uint8Array([1])).i32(), /Truncated/)
    assert.throws(() => new WireReader(new Uint8Array([1])).finish(), /Trailing/)
})

test('typed call posts bytes and decodes native response', async () => {
    const originalFetch = globalThis.fetch
    const originalWindow = globalThis.window
    try {
        globalThis.window = { __APP_BINARY_RPC__: true }
        globalThis.fetch = async (url, options) => {
            assert.equal(url, 'app-rpc://native/1')
            assert.equal(options.method, 'POST')
            assert.equal(options.body.byteLength, 0)
            return new Response(new WireWriter().i32(42).finish())
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
        globalThis.window = { __APP_BINARY_RPC__: true }
        const source = new Uint8Array([0, 255, 17, 42])
        globalThis.fetch = async (url, options) => {
            assert.equal(url, 'app-rpc://native/5')
            const input = new WireReader(options.body)
            assert.deepEqual(input.bytes(), source)
            input.finish()
            return new Response(new WireWriter().bytes(source).finish())
        }
        assert.deepEqual(await echoBytesBinary(source), source)
    } finally {
        globalThis.fetch = originalFetch
        globalThis.window = originalWindow
    }
})
