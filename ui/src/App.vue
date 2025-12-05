<script setup>
import { ref, computed } from 'vue'

const message = ref('')
const response = ref('')
const history = ref([])
const counter = ref(0)

const historyCount = computed(() => history.value.length)

async function sendMessage() {
    if (!message.value.trim()) return

    try {
        if (window.ping) {
            window.ping(message.value)
            history.value.push({
                id: Date.now(),
                text: message.value,
                time: new Date().toLocaleTimeString()
            })
            response.value = `✅ Enviado: "${message.value}"`
            message.value = ''
        } else {
            response.value = '❌ Backend C++ não conectado'
        }
    } catch (error) {
        response.value = `❌ Erro: ${error.message}`
    }
}

function increment() {
    counter.value++
}

function decrement() {
    counter.value--
}

function clearHistory() {
    history.value = []
    response.value = ''
}
</script>

<template>
    <div class="container">
        <header>
            <h1>🚀 Vue 3 + WebView</h1>
            <p class="subtitle">Hot Reload em tempo real!</p>
        </header>

        <!-- Contador interativo -->
        <section class="card">
            <h2>⚡ Contador Reativo</h2>
            <div class="counter">
                <button class="btn-round" @click="decrement">−</button>
                <span class="counter-value" :class="{ negative: counter < 0, positive: counter > 0 }">
                    {{ counter }}
                </span>
                <button class="btn-round" @click="increment">+</button>
            </div>
        </section>

        <!-- Comunicação com C++ -->
        <section class="card">
            <h2>💬 Mensagem para C++</h2>
            <div class="input-group">
                <input v-model="message" type="text" placeholder="Digite algo..." @keyup.enter="sendMessage" />
                <button @click="sendMessage">Enviar</button>
            </div>
            <p v-if="response" class="response">{{ response }}</p>
        </section>

        <!-- Histórico -->
        <section v-if="historyCount > 0" class="card">
            <div class="history-header">
                <h2>📜 Histórico ({{ historyCount }})</h2>
                <button class="btn-small" @click="clearHistory">Limpar</button>
            </div>
            <TransitionGroup name="list" tag="ul" class="history-list">
                <li v-for="item in history" :key="item.id">
                    <span class="time">{{ item.time }}</span>
                    <span class="text">{{ item.text }}</span>
                </li>
            </TransitionGroup>
        </section>

        <footer>
            <p>Feito com 💚 Vue {{ version }}</p>
        </footer>
    </div>
</template>

<script>
export default {
    data() {
        return {
            version: '3.5'
        }
    }
}
</script>

<style scoped>
.container {
    max-width: 600px;
    margin: 0 auto;
    padding: 2rem;
}

header {
    text-align: center;
    margin-bottom: 2rem;
}

h1 {
    color: #42b883;
    margin-bottom: 0.5rem;
}

.subtitle {
    color: #888;
    font-size: 0.9rem;
}

h2 {
    color: #fff;
    font-size: 1rem;
    margin-bottom: 1rem;
}

.card {
    background: #1a1a1a;
    border-radius: 12px;
    padding: 1.5rem;
    margin-bottom: 1rem;
}

/* Contador */
.counter {
    display: flex;
    align-items: center;
    justify-content: center;
    gap: 1.5rem;
}

.btn-round {
    width: 48px;
    height: 48px;
    border-radius: 50%;
    border: 2px solid #42b883;
    background: transparent;
    color: #42b883;
    font-size: 1.5rem;
    cursor: pointer;
    transition: all 0.2s;
}

.btn-round:hover {
    background: #42b883;
    color: #000;
}

.counter-value {
    font-size: 3rem;
    font-weight: bold;
    min-width: 100px;
    text-align: center;
    transition: color 0.2s;
}

.counter-value.positive {
    color: #42b883;
}

.counter-value.negative {
    color: #f56c6c;
}

/* Input group */
.input-group {
    display: flex;
    gap: 0.5rem;
}

input {
    flex: 1;
    padding: 0.75rem 1rem;
    border-radius: 8px;
    border: 1px solid #333;
    background: #0d0d0d;
    color: #fff;
    font-size: 1rem;
}

input:focus {
    outline: none;
    border-color: #42b883;
}

button {
    padding: 0.75rem 1.5rem;
    border-radius: 8px;
    border: none;
    background: #42b883;
    color: #fff;
    font-weight: 600;
    cursor: pointer;
    transition: background 0.2s;
}

button:hover {
    background: #33a06f;
}

.btn-small {
    padding: 0.5rem 1rem;
    font-size: 0.8rem;
    background: #333;
}

.btn-small:hover {
    background: #444;
}

.response {
    margin-top: 1rem;
    padding: 0.75rem;
    background: #0d0d0d;
    border-radius: 6px;
    font-size: 0.9rem;
}

/* Histórico */
.history-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 1rem;
}

.history-header h2 {
    margin: 0;
}

.history-list {
    list-style: none;
    padding: 0;
    margin: 0;
    max-height: 200px;
    overflow-y: auto;
}

.history-list li {
    display: flex;
    gap: 1rem;
    padding: 0.5rem 0;
    border-bottom: 1px solid #333;
}

.history-list li:last-child {
    border-bottom: none;
}

.time {
    color: #666;
    font-size: 0.8rem;
    font-family: monospace;
}

.text {
    color: #ccc;
}

/* Animações */
.list-enter-active,
.list-leave-active {
    transition: all 0.3s ease;
}

.list-enter-from {
    opacity: 0;
    transform: translateX(-30px);
}

.list-leave-to {
    opacity: 0;
    transform: translateX(30px);
}

/* Footer */
footer {
    text-align: center;
    margin-top: 2rem;
    color: #666;
    font-size: 0.8rem;
}
</style>
