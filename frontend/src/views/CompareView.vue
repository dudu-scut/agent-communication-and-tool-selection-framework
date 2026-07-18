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
        <button class="btn-compare" disabled>
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polygon points="5 3 19 12 5 21 5 3"/></svg>
          Compare (Coming Soon)
        </button>
      </div>
    </div>

    <div class="results-area">
      <GlassCard variant="highlight" padding="lg">
        <EmptyState
          icon="mdi:flask-outline"
          title="Agent 对比功能开发中"
          description="多 Agent 侧边对比功能正在开发中，敬请期待"
        />
      </GlassCard>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { getAgents } from '../services/grpc-client'
import GlassCard from '../components/layout/GlassCard.vue'
import EmptyState from '../components/feedback/EmptyState.vue'

const availableAgents = ref<string[]>([])
const selectedAgents = ref<string[]>(['', '', ''])
const compareQuery = ref('')

onMounted(async () => {
  try {
    const resp = await getAgents()
    availableAgents.value = resp.agents.map(a => a.service_name)
  } catch (e) {
    console.warn('Failed to load agents:', e)
  }
})


</script>

<style scoped>
@import "../styles/design-tokens.css";

.compare-view { min-height: 100vh; background: var(--bg-primary); }

.compare-header {
  display: flex; align-items: center; gap: 12px;
  padding: 20px 32px; background: var(--bg-elevated); border-bottom: 1px solid var(--border-default);
}

.back-link { display: flex; color: var(--text-secondary); padding: 4px; border-radius: var(--radius-sm); transition: all 0.15s; }
.back-link:hover { background: var(--glass-bg-hover); }

.compare-header h1 { font-size: 22px; font-weight: 700; margin: 0; color: var(--text-primary); }
.subtitle { font-size: 13px; color: var(--text-tertiary); }

.compare-controls {
  padding: 24px 32px; background: var(--bg-elevated); border-bottom: 1px solid var(--border-default);
  display: flex; gap: 20px; flex-wrap: wrap; align-items: flex-end;
}

.control-group { display: flex; flex-direction: column; gap: 8px; }
.control-group label { font-size: 12px; color: var(--text-secondary); font-weight: 500; }

.agent-selects { display: flex; gap: 8px; }

.select-input {
  padding: 8px 12px; border: 1px solid var(--border-default); border-radius: var(--radius-sm);
  font-size: 13px; background: var(--bg-surface); color: var(--text-primary); outline: none; min-width: 160px;
}

.select-input:focus { border-color: var(--brand-primary); }

.query-group { display: flex; gap: 10px; align-items: flex-end; flex: 1; }

.query-input {
  flex: 1; padding: 10px 14px; border: 1px solid var(--border-default); border-radius: var(--radius-md);
  font-size: 14px; font-family: inherit; resize: vertical; outline: none;
  min-width: 200px; background: var(--bg-surface); color: var(--text-primary);
}

.query-input:focus { border-color: var(--brand-primary); box-shadow: 0 0 0 3px rgba(99,102,241,0.1); }

.btn-compare {
  display: flex; align-items: center; gap: 8px;
  padding: 10px 20px; border: none; border-radius: var(--radius-md);
  background: var(--brand-primary); color: #fff; font-size: 14px; font-weight: 600; cursor: pointer;
}

.btn-compare:disabled { opacity: 0.5; cursor: not-allowed; }

.results-area { padding: 24px 32px; max-width: 1400px; }

/* Responsive */
@media (max-width: 1024px) {
  .compare-controls {
    padding: 20px 24px;
  }
  .results-area {
    padding: 20px 24px;
  }
}

@media (max-width: 768px) {
  .compare-header {
    padding: 16px 20px;
  }
  .compare-controls {
    flex-direction: column;
    padding: 16px 20px;
  }
  .agent-selects {
    flex-direction: column;
  }
  .select-input {
    min-width: auto;
    width: 100%;
  }
  .query-group {
    flex-direction: column;
  }
  .query-input {
    min-width: auto;
  }
  .results-area {
    padding: 16px 20px;
  }
}

@media (max-width: 480px) {
  .compare-header h1 {
    font-size: 18px;
  }
  .subtitle {
    display: none;
  }
}
</style>
