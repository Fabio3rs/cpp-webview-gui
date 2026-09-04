<script setup>
import { onBeforeUnmount, onMounted, ref } from 'vue'

const canvasRef = ref(null)
let gl = null
let program = null
let vertexBuffer = null

const vertexSource = `
attribute vec2 aPosition;
void main() {
    gl_Position = vec4(aPosition, 0.0, 1.0);
}
`

const fragmentSource = `
precision mediump float;
void main() {
    gl_FragColor = vec4(0.24, 0.63, 0.95, 1.0);
}
`

function createShader(type, source) {
    if (!gl) return null
    const shader = gl.createShader(type)
    if (!shader) return null
    gl.shaderSource(shader, source)
    gl.compileShader(shader)
    const ok = gl.getShaderParameter(shader, gl.COMPILE_STATUS)
    if (ok) {
        return shader
    }
    console.warn('WebGL shader error:', gl.getShaderInfoLog(shader))
    gl.deleteShader(shader)
    return null
}

function createProgram(vertexShader, fragmentShader) {
    if (!gl) return null
    const nextProgram = gl.createProgram()
    if (!nextProgram) return null
    gl.attachShader(nextProgram, vertexShader)
    gl.attachShader(nextProgram, fragmentShader)
    gl.linkProgram(nextProgram)
    const ok = gl.getProgramParameter(nextProgram, gl.LINK_STATUS)
    if (ok) {
        return nextProgram
    }
    console.warn('WebGL link error:', gl.getProgramInfoLog(nextProgram))
    gl.deleteProgram(nextProgram)
    return null
}

function resizeCanvas(canvas) {
    const dpr = window.devicePixelRatio || 1
    const displayWidth = Math.floor(canvas.clientWidth * dpr)
    const displayHeight = Math.floor(canvas.clientHeight * dpr)
    if (canvas.width !== displayWidth || canvas.height !== displayHeight) {
        canvas.width = displayWidth
        canvas.height = displayHeight
    }
}

function drawTriangle() {
    if (!gl || !program || !vertexBuffer) return
    gl.viewport(0, 0, gl.drawingBufferWidth, gl.drawingBufferHeight)
    gl.clearColor(0.05, 0.08, 0.14, 1.0)
    gl.clear(gl.COLOR_BUFFER_BIT)
    gl.drawArrays(gl.TRIANGLES, 0, 3)
}

function handleResize() {
    if (!canvasRef.value || !gl) return
    resizeCanvas(canvasRef.value)
    drawTriangle()
}

function initWebgl() {
    const canvas = canvasRef.value
    if (!canvas) return
    gl = canvas.getContext('webgl')
    if (!gl) {
        console.warn('WebGL não suportado neste navegador.')
        return
    }

    resizeCanvas(canvas)

    const vertexShader = createShader(gl.VERTEX_SHADER, vertexSource)
    const fragmentShader = createShader(gl.FRAGMENT_SHADER, fragmentSource)
    if (!vertexShader || !fragmentShader) return

    program = createProgram(vertexShader, fragmentShader)
    if (!program) return

    gl.useProgram(program)

    vertexBuffer = gl.createBuffer()
    gl.bindBuffer(gl.ARRAY_BUFFER, vertexBuffer)
    gl.bufferData(
        gl.ARRAY_BUFFER,
        new Float32Array([0.0, 0.75, -0.75, -0.75, 0.75, -0.75]),
        gl.STATIC_DRAW
    )

    const positionLocation = gl.getAttribLocation(program, 'aPosition')
    gl.enableVertexAttribArray(positionLocation)
    gl.vertexAttribPointer(positionLocation, 2, gl.FLOAT, false, 0, 0)

    drawTriangle()
}

onMounted(() => {
    initWebgl()
    window.addEventListener('resize', handleResize)
})

onBeforeUnmount(() => {
    window.removeEventListener('resize', handleResize)
    if (gl && vertexBuffer) {
        gl.deleteBuffer(vertexBuffer)
    }
    if (gl && program) {
        gl.deleteProgram(program)
    }
    gl = null
    program = null
    vertexBuffer = null
})
</script>

<template>
    <div class="hello-webgl">
        <canvas ref="canvasRef" class="hello-webgl__canvas"></canvas>
        <p class="hello-webgl__caption">
            Hello WebGL — triângulo renderizado diretamente no canvas central.
        </p>
    </div>
</template>

<style scoped>
.hello-webgl {
    width: 100%;
    max-width: 860px;
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 0.6rem;
}

.hello-webgl__canvas {
    width: min(90vw, 780px);
    aspect-ratio: 16 / 10;
    display: block;
    border-radius: 16px;
    border: 1px solid rgba(120, 150, 200, 0.35);
    background: radial-gradient(
            circle at 20% 20%,
            rgba(80, 138, 255, 0.2),
            transparent 35%
        ),
        radial-gradient(circle at 80% 30%, rgba(120, 210, 255, 0.15), transparent 45%),
        #0b101c;
    box-shadow: 0 20px 45px rgba(2, 6, 12, 0.65);
}

.hello-webgl__caption {
    color: rgba(223, 233, 248, 0.7);
    font-size: 0.9rem;
    text-align: center;
    max-width: 640px;
}
</style>
