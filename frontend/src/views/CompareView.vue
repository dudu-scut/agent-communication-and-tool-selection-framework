<template>
  <div class="compare-view">
    <div class="compare-header">
      <router-link to="/" class="back-link">
        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="15 18 9 12 15 6"/></svg>
      </router-link>
      <h1>Agent Compare</h1>
      <span class="subtitle">Compare Agent responses side by side</span>
    </div>

    <div class="compare-controls">
      <div class="control-group">
        <label>Select Agents (up to 3)</label>
        <div class="agent-selects">
          <select v-model="selectedAgents[0]" class="select-input">
            <option value="">-- Select Agent --</option>
            <option v-for="a in availableAgents" :key="a" :value="a">{{ a }}</option>
          </select>
          <select v-model="selectedAgents[1]" class="select-input">
            <option value="">-- Select Agent --</option>
            <option v-for="a in availableAgents" :key="a" :value="a">{{ a }}</option>
          </select>
          <select v-model="selectedAgents[2]" class="select-input">
            <option value="">-- Select Agent --</option>
            <option v-for="a in availableAgents" :key="a" :value="a">{{ a }}</option>
          </select>
        </div>
      </div>
      <div class="query-group">
        <textarea v-model="compareQuery" placeholder="Enter comparison query..." class="query-input" rows="3"></textarea>
        <button class="btn-compare" @click="runCompare" :disabled="!canCompare">
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polygon points="5 3 19 12 5 21 5 3"/></svg>
          Compare
        </button>
      </div>
    </div>

    <div v-if="results.length > 0" class="results-area">
      <div class="columns">
        <div v-for="(result, i) in results" :key="i" class="result-col">
          <div class="col-header">
            <span class="col-agent-name">{{ result.agent }}</span>
          </div>
          <div class="col-body">
            <div class="col-response">{{ result.content }}</div>
          </div>
        </div>
      </div>

      <!-- Comparison Summary -->
      <div class="compare-summary">
        <h3>Comparison Summary</h3>
        <div class="summary-grid">
          <div v-for="r in results" :key="r.agent" class="summary-card">
            <div class="summary-agent">{{ r.agent }}</div>
            <div class="summary-metrics">
              <div class="sm">
                <span class="sm-label">Latency</span>
                <span class="sm-value">{{ r.latency }}ms</span>
              </div>
              <div class="sm">
                <span class="sm-label">Cost</span>
                <span class="sm-value">{{ formatTokens(r.cost) }}</span>
              </div>
              <div class="sm">
                <span class="sm-label">Length</span>
                <span class="sm-value">{{ r.content.length }} chars</span>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed } from 'vue'

interface CompareResult {
  agent: string
  content: string
  latency: number
  cost: number
}

const availableAgents = ['mock-general', 'mock-translator', 'mock-writer', 'mock-analyst', 'mock-coder']
const selectedAgents = ref<string[]>(['mock-general', 'mock-translator', ''])
const compareQuery = ref('')
const results = ref<CompareResult[]>([])

const canCompare = computed(() =>
  compareQuery.value.trim() && selectedAgents.value.filter(Boolean).length >= 1
)

async function runCompare() {
  const agents = selectedAgents.value.filter(Boolean)
  results.value = []
  for (const agent of agents) {
    const start = Date.now()
    await new Promise(r => setTimeout(r, 600 + Math.random() * 800))
    results.value.push({
      agent,
      content: `[${agent}] Response to "${compareQuery.value}":\n\n` +
        'This is a simulated Agent comparison response, demonstrating different answer styles and depth across Agents for the same query.\n\n' +
        'In production, real multi-Agent side-by-side comparison results would be shown here.',
      latency: Date.now() - start,
      cost: Math.floor(Math.random() * 5000 + 500),
    })
  }
}

function formatTokens(n: number): string {
  return n >= 1000 ? (n / 1000).toFixed(1) + 'K' : n.toString()
}
</script>

<style scoped>
.compare-view { min-height: 100vh; background: #f8fafc; }

.compare-header {
  display: flex; align-items: center; gap: 12px;
  padding: 20px 32px; background: #fff; border-bottom: 1px solid #e5e7eb;
}

.back-link { display: flex; color: #6b7280; padding: 4px; border-radius: 6px; }
.back-link:hover { background: #f3f4f6; }

.compare-header h1 { font-size: 22px; font-weight: 700; margin: 0; }
.subtitle { font-size: 13px; color: #9ca3af; }

.compare-controls {
  padding: 24px 32px; background: #fff; border-bottom: 1px solid #e5e7eb;
  display: flex; gap: 20px; flex-wrap: wrap; align-items: flex-end;
}

.control-group { display: flex; flex-direction: column; gap: 8px; }
.control-group label { font-size: 12px; color: #6b7280; font-weight: 500; }

.agent-selects { display: flex; gap: 8px; }

.select-input {
  padding: 8px 12px; border: 1px solid #d1d5db; border-radius: 8px;
  font-size: 13px; background: #fff; outline: none; min-width: 160px;
}

.select-input:focus { border-color: #6366f1; }

.query-group { display: flex; gap: 10px; align-items: flex-end; flex: 1; }

.query-input {
  flex: 1; padding: 10px 14px; border: 1px solid #d1d5db; border-radius: 10px;
  font-size: 14px; font-family: inherit; resize: vertical; outline: none;
  min-width: 200px;
}

.query-input:focus { border-color: #6366f1; box-shadow: 0 0 0 3px rgba(99,102,241,0.1); }

.btn-compare {
  display: flex; align-items: center; gap: 8px;
  padding: 10px 20px; border: none; border-radius: 10px;
  background: #6366f1; color: #fff; font-size: 14px; font-weight: 600; cursor: pointer;
}

.btn-compare:disabled { background: #c7d2fe; cursor: not-allowed; }

.results-area { padding: 24px 32px; max-width: 1400px; }

.columns { display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 16px; margin-bottom: 28px; }

.result-col {
  background: #fff; border: 1px solid #e5e7eb; border-radius: 12px; overflow: hidden;
}

.col-header {
  padding: 12px 16px; background: #f9fafb; border-bottom: 1px solid #e5e7eb;
  font-weight: 600; font-size: 14px;
}

.col-body { padding: 16px; }

.col-response {
  font-size: 14px; line-height: 1.6; white-space: pre-wrap; color: #374151;
}

.compare-summary { background: #fff; border: 1px solid #e5e7eb; border-radius: 12px; padding: 20px; }

.compare-summary h3 { font-size: 15px; font-weight: 600; color: #1f2937; margin: 0 0 14px; }

.summary-grid { display: flex; gap: 16px; flex-wrap: wrap; }

.summary-card {
  flex: 1; min-width: 200px;
  padding: 14px; background: #fafafa; border-radius: 10px;
}

.summary-agent { font-weight: 600; font-size: 14px; color: #1f2937; margin-bottom: 10px; }

.summary-metrics { display: flex; gap: 16px; }

.sm { display: flex; flex-direction: column; gap: 2px; }
.sm-label { font-size: 11px; color: #9ca3af; }
.sm-value { font-size: 14px; font-weight: 600; color: #374151; font-family: monospace; }
</style>
