import { decodeCbor, encodeCbor } from './cbor.js'

const stringFields = ['windowId', 'title', 'url']
const numberFields = ['width', 'height', 'left', 'top']

export function writeOpaque(writer, value) {
    return writer.bytes(encodeCbor(value))
}

export function readOpaque(reader) {
    return decodeCbor(reader.bytes())
}

export function writeBootstrap(writer, bootstrap) {
    const source = bootstrap && typeof bootstrap === 'object' && !Array.isArray(bootstrap)
        ? bootstrap : {}
    const extras = { ...source }
    for (const field of stringFields) {
        const value = typeof source[field] === 'string' ? source[field] : null
        writer.optional(value, (output, item) => output.string(item))
        if (value !== null) delete extras[field]
    }
    for (const field of numberFields) {
        const value = typeof source[field] === 'number' && Number.isFinite(source[field])
            ? source[field] : null
        writer.optional(value, (output, item) => output.f64(item))
        if (value !== null) delete extras[field]
    }
    writeOpaque(writer, extras)
    return writer
}

export function readBootstrap(reader) {
    const result = {}
    for (const field of stringFields) {
        const value = reader.optional(input => input.string())
        if (value !== null) result[field] = value
    }
    for (const field of numberFields) {
        const value = reader.optional(input => input.f64())
        if (value !== null) result[field] = value
    }
    const extras = readOpaque(reader)
    if (extras && typeof extras === 'object' && !Array.isArray(extras)) {
        Object.assign(result, extras)
    }
    return result
}

export function readWindowList(reader) {
    return reader.vector(input => ({ id: input.string(), title: input.string() }))
}

export function readOutsideDrop(reader) {
    return reader.optional(input => {
        const result = { payload: readOpaque(input) }
        const drop = input.optional(value => ({ x: value.f64(), y: value.f64() }))
        if (drop !== null) result.drop = drop
        return result
    })
}
