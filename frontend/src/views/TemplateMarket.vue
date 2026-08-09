<template>
  <div class="template-view">
    <div class="template-header">
      <router-link to="/" class="back-link">
        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="15 18 9 12 15 6"/></svg>
      </router-link>
      <div class="header-text">
        <h1>Template Market</h1>
        <span class="subtitle">Use preset templates to quickly create Agent workflows</span>
      </div>
      <button class="new-btn" @click="showCreateForm = !showCreateForm">
        {{ showCreateForm ? 'Close' : 'New Template' }}
      </button>
    </div>

    <div class="template-body">
      <!-- Create form -->
      <GlassCard v-if="showCreateForm" variant="highlight" padding="lg" class="create-card">
        <h3>Save Template</h3>
        <div class="form-row">
          <label>Name</label>
          <input v-model="formName" class="text-input" placeholder="Template name" />
        </div>
        <div class="form-row">
          <label>Description</label>
          <input v-model="formDescription" class="text-input" placeholder="What this template does" />
        </div>
        <div class="form-row">
          <label>Definition (JSON)</label>
          <textarea
            v-model="formDefinition"
            class="json-input"
            rows="6"
            placeholder='{"initial_message": "..." }'
          ></textarea>
        </div>
        <div v-if="saveError" class="form-error">{{ saveError }}</div>
        <div v-if="saveSuccess" class="form-success">{{ saveSuccess }}</div>
        <button class="save-btn" :disabled="saving" @click="save">
          {{ saving ? 'Saving…' : 'Save Template' }}
        </button>
      </GlassCard>

      <!-- Loading -->
      <div v-if="state === 'loading'" class="state-block">
        <div class="spinner" />
        <p>Loading templates…</p>
      </div>

      <!-- Error -->
      <div v-else-if="state === 'error'" class="state-block error">
        <h3>Failed to load templates</h3>
        <p class="error-message">{{ errorMessage }}</p>
        <button class="retry-btn" @click="load">Retry</button>
      </div>

      <!-- Empty -->
      <GlassCard v-else-if="state === 'empty'" variant="highlight" padding="lg" class="empty-card">
        <EmptyState
          icon="mdi:file-document-outline"
          title="No templates yet"
          description="You have not saved any templates. Use “New Template” to create one."
        />
      </GlassCard>

      <!-- Result -->
      <div v-else class="template-grid">
        <GlassCard
          v-for="tpl in templates"
          :key="tpl.template_id"
          variant="highlight"
          padding="lg"
          class="template-card"
        >
          <div class="tpl-head">
            <h3>{{ tpl.name }}</h3>
            <span class="tpl-version">v{{ tpl.version }}</span>
          </div>
          <p class="tpl-desc">{{ tpl.description || '—' }}</p>
          <pre class="tpl-def">{{ prettyDefinition(tpl.definition) }}</pre>
          <div v-if="useResults[tpl.template_id]" class="use-result" :class="useResults[tpl.template_id].ok ? 'ok' : 'err'">
            {{ useResults[tpl.template_id].message }}
          </div>
          <button
            class="use-btn"
            :disabled="usingIds.has(tpl.template_id)"
            @click="use(tpl)"
          >
            {{ usingIds.has(tpl.template_id) ? 'Creating…' : 'Use Template' }}
          </button>
        </GlassCard>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, onMounted } from 'vue'
import GlassCard from '../components/layout/GlassCard.vue'
import EmptyState from '../components/feedback/EmptyState.vue'
import { listTemplates, saveTemplate, useTemplate } from '../services/grpc-client'
import type { TemplateEntry } from '../types/proto'

const state = ref<'loading' | 'error' | 'empty' | 'result'>('loading')
const errorMessage = ref('')
const templates = ref<TemplateEntry[]>([])

const showCreateForm = ref(false)
const formName = ref('')
const formDescription = ref('')
const formDefinition = ref('')
const saving = ref(false)
const saveError = ref('')
const saveSuccess = ref('')

const usingIds = reactive(new Set<string>())
const useResults = reactive<Record<string, { ok: boolean; message: string }>>({})

async function load() {
  state.value = 'loading'
  errorMessage.value = ''
  try {
    const resp = await listTemplates()
    const list = resp.templates ?? []
    templates.value = list
    state.value = list.length > 0 ? 'result' : 'empty'
  } catch (e) {
    state.value = 'error'
    errorMessage.value = e instanceof Error ? e.message : String(e)
  }
}

function prettyDefinition(definition: string): string {
  try {
    return JSON.stringify(JSON.parse(definition), null, 2)
  } catch {
    return definition
  }
}

async function save() {
  saveError.value = ''
  saveSuccess.value = ''
  saving.value = true
  try {
    const resp = await saveTemplate(formName.value, formDescription.value, formDefinition.value)
    saveSuccess.value = `Template saved (${resp.template_id})`
    formName.value = ''
    formDescription.value = ''
    formDefinition.value = ''
    await load()
  } catch (e) {
    saveError.value = e instanceof Error ? e.message : String(e)
  } finally {
    saving.value = false
  }
}

async function use(tpl: TemplateEntry) {
  usingIds.add(tpl.template_id)
  useResults[tpl.template_id] = { ok: true, message: '' }
  try {
    const resp = await useTemplate(tpl.template_id)
    useResults[tpl.template_id] = {
      ok: true,
      message: `Conversation created (context: ${resp.context_id}). Start chatting to continue.`,
    }
  } catch (e) {
    useResults[tpl.template_id] = {
      ok: false,
      message: e instanceof Error ? e.message : String(e),
    }
  } finally {
    usingIds.delete(tpl.template_id)
  }
}

onMounted(load)
</script>

<style scoped>
@import "../styles/design-tokens.css";

.template-view { min-height: 100vh; background: var(--bg-primary); }

.template-header {
  display: flex; align-items: center; gap: 14px;
  padding: 20px 32px; background: var(--bg-elevated); border-bottom: 1px solid var(--border-default);
}

.back-link { display: flex; color: var(--text-secondary); padding: 4px; border-radius: var(--radius-sm); transition: all 0.15s; }
.back-link:hover { background: var(--glass-bg-hover); }

.header-text { flex: 1; }

.template-header h1 { font-size: 22px; font-weight: 700; margin: 0 0 2px;
  background: linear-gradient(135deg, var(--color-warning), var(--color-error));
  -webkit-background-clip: text; -webkit-text-fill-color: transparent; background-clip: text;
}

.subtitle { font-size: 13px; color: var(--text-tertiary); }

.new-btn {
  padding: 8px 18px; border-radius: var(--radius-md); border: 1px solid var(--border-default);
  background: var(--bg-elevated); color: var(--text-primary); cursor: pointer;
}
.new-btn:hover { background: var(--glass-bg-hover); }

.template-body {
  padding: 28px 32px;
  max-width: 1200px;
}

.create-card { margin-bottom: 20px; }
.create-card h3 { margin: 0 0 14px; color: var(--text-primary); }

.form-row { display: flex; flex-direction: column; gap: 6px; margin-bottom: 12px; }
.form-row label { font-size: 12px; color: var(--text-tertiary); }

.text-input {
  padding: 9px 12px; border-radius: var(--radius-md);
  border: 1px solid var(--border-default); background: var(--bg-primary);
  color: var(--text-primary); font-size: 14px;
}

.json-input {
  padding: 9px 12px; border-radius: var(--radius-md);
  border: 1px solid var(--border-default); background: var(--bg-primary);
  color: var(--text-primary); font-size: 13px; font-family: monospace; resize: vertical;
}

.form-error { color: var(--color-error); font-size: 13px; margin-bottom: 10px; }
.form-success { color: var(--color-success); font-size: 13px; margin-bottom: 10px; }

.save-btn {
  padding: 9px 22px; border-radius: var(--radius-md); border: none;
  background: linear-gradient(135deg, var(--color-warning), var(--color-error));
  color: #fff; font-weight: 600; cursor: pointer;
}
.save-btn:disabled { opacity: 0.6; cursor: not-allowed; }

.state-block {
  display: flex; flex-direction: column; align-items: center; gap: 12px;
  padding: 48px 0; color: var(--text-secondary);
}
.state-block.error h3 { color: var(--color-error); margin: 0; }
.error-message { margin: 0; }

.retry-btn {
  padding: 8px 20px; border-radius: var(--radius-md); border: 1px solid var(--border-default);
  background: var(--bg-elevated); color: var(--text-primary); cursor: pointer;
}

.spinner {
  width: 28px; height: 28px;
  border: 3px solid var(--border-default);
  border-top-color: var(--color-warning);
  border-radius: 50%;
  animation: spin 0.9s linear infinite;
}
@keyframes spin { to { transform: rotate(360deg); } }

.empty-card { margin-bottom: 20px; }

.template-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(320px, 1fr));
  gap: 20px;
}

.template-card { display: flex; flex-direction: column; gap: 10px; }

.tpl-head { display: flex; align-items: center; justify-content: space-between; }
.tpl-head h3 { margin: 0; font-size: 16px; color: var(--text-primary); }
.tpl-version { font-size: 12px; color: var(--text-tertiary); }

.tpl-desc { margin: 0; font-size: 13px; color: var(--text-secondary); }

.tpl-def {
  margin: 0; padding: 10px; border-radius: var(--radius-sm);
  background: var(--bg-primary); border: 1px solid var(--border-default);
  font-size: 12px; color: var(--text-secondary);
  max-height: 160px; overflow: auto; white-space: pre-wrap; word-break: break-word;
}

.use-result { font-size: 13px; }
.use-result.ok { color: var(--color-success); }
.use-result.err { color: var(--color-error); }

.use-btn {
  padding: 9px 18px; border-radius: var(--radius-md); border: 1px solid var(--border-default);
  background: var(--bg-elevated); color: var(--text-primary); cursor: pointer; font-weight: 600;
}
.use-btn:hover:not(:disabled) { background: var(--glass-bg-hover); }
.use-btn:disabled { opacity: 0.6; cursor: not-allowed; }

/* Responsive */
@media (max-width: 768px) {
  .template-header { padding: 16px 20px; }
  .template-body { padding: 16px 20px; }
}

@media (max-width: 480px) {
  .template-header h1 { font-size: 18px; }
}
</style>
