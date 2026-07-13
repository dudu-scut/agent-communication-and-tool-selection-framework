<template>
  <div class="template-view">
    <div class="template-header">
      <router-link to="/" class="back-link">
        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="15 18 9 12 15 6"/></svg>
      </router-link>
      <div>
        <h1>Template Market</h1>
        <span class="subtitle">Use preset templates to quickly create Agent workflows</span>
      </div>
    </div>

    <div class="template-filters">
      <button
        v-for="cat in categories"
        :key="cat"
        class="filter-btn"
        :class="{ active: activeCategory === cat }"
        @click="activeCategory = cat"
      >{{ cat }}</button>
    </div>

    <div class="template-grid">
      <div v-for="tmpl in filteredTemplates" :key="tmpl.id" class="template-card">
        <div class="tmpl-badge" :class="tmpl.category">{{ tmpl.category }}</div>
        <div class="tmpl-content">
          <h3>{{ tmpl.name }}</h3>
          <p>{{ tmpl.description }}</p>
          <div class="tmpl-meta">
            <span>📋 {{ tmpl.taskCount }} subtask(s)</span>
            <span>⭐ {{ tmpl.rating }}</span>
            <span>👥 {{ formatCount(tmpl.usageCount) }} uses</span>
          </div>
          <div class="tmpl-author">by {{ tmpl.author }}</div>
          <button class="use-btn" @click="useTemplate(tmpl)">
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M5 12h14"/><polyline points="12 5 19 12 12 19"/></svg>
            Use Template
          </button>
        </div>
      </div>
    </div>

    <!-- Use Confirmation -->
    <Teleport to="body">
      <div v-if="activeTemplate" class="modal-overlay" @click.self="activeTemplate = null">
        <div class="modal-card">
          <div class="modal-header">
            <span>Use Template: {{ activeTemplate.name }}</span>
            <button class="modal-close" @click="activeTemplate = null">
              <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/></svg>
            </button>
          </div>
          <div class="modal-body">
            <p class="modal-desc">This template contains {{ activeTemplate.taskCount }} predefined subtask(s) in a DAG structure. A new session will be created and the workflow preloaded.</p>
            <div class="modal-actions">
              <button class="btn-cancel" @click="activeTemplate = null">Cancel</button>
              <button class="btn-confirm" @click="confirmUse">
                <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="20 6 9 17 4 12"/></svg>
                Confirm
              </button>
            </div>
          </div>
        </div>
      </div>
    </Teleport>
  </div>
</template>

<script setup lang="ts">
import { ref, computed } from 'vue'
import { useRouter } from 'vue-router'

const router = useRouter()

interface Template {
  id: string
  name: string
  description: string
  category: string
  taskCount: number
  rating: number
  usageCount: number
  author: string
}

const categories = ['All', 'Dev', 'Analytics', 'Writing', 'Translate', 'Planning']
const activeCategory = ref('All')

const templates = ref<Template[]>([
  { id: '1', name: 'Code Review Workflow', description: 'Automated code review for bugs, security vulnerabilities, and performance issues', category: 'Dev', taskCount: 4, rating: 4.8, usageCount: 3200, author: 'dev-team' },
  { id: '2', name: 'Data Analysis Report', description: 'Connect data source → clean → analyze → generate visualization report', category: 'Analytics', taskCount: 5, rating: 4.6, usageCount: 2100, author: 'data-team' },
  { id: '3', name: 'Multi-language Translation Chain', description: 'Source → intermediate → target language with terminology consistency check', category: 'Translate', taskCount: 3, rating: 4.7, usageCount: 1800, author: 'l10n-team' },
  { id: '4', name: 'Technical Blog Generator', description: 'Research → outline → draft → polish → SEO optimization', category: 'Writing', taskCount: 5, rating: 4.9, usageCount: 4500, author: 'content-team' },
  { id: '5', name: 'Microservice Migration Plan', description: 'Current state analysis → service split proposals → migration steps → risk assessment', category: 'Planning', taskCount: 4, rating: 4.5, usageCount: 890, author: 'arch-team' },
  { id: '6', name: 'API Documentation Generator', description: 'Auto-generate OpenAPI/Swagger docs from code annotations', category: 'Dev', taskCount: 2, rating: 4.4, usageCount: 5600, author: 'api-team' },
])

const filteredTemplates = computed(() => {
  if (activeCategory.value === 'All') return templates.value
  return templates.value.filter(t => t.category === activeCategory.value)
})

const activeTemplate = ref<Template | null>(null)

function useTemplate(tmpl: Template) {
  activeTemplate.value = tmpl
}

function confirmUse() {
  activeTemplate.value = null
  router.push('/')
}

function formatCount(n: number): string {
  return n >= 1000 ? (n / 1000).toFixed(1) + 'k' : n.toString()
}
</script>

<style scoped>
.template-view { min-height: 100vh; background: #f8fafc; }

.template-header {
  display: flex; align-items: center; gap: 14px;
  padding: 20px 32px; background: #fff; border-bottom: 1px solid #e5e7eb;
}

.back-link { display: flex; color: #6b7280; padding: 4px; border-radius: 6px; }
.back-link:hover { background: #f3f4f6; }

.template-header h1 { font-size: 22px; font-weight: 700; margin: 0 0 2px;
  background: linear-gradient(135deg, #f59e0b, #ef4444);
  -webkit-background-clip: text; -webkit-text-fill-color: transparent; background-clip: text;
}

.subtitle { font-size: 13px; color: #9ca3af; }

.template-filters {
  display: flex; gap: 8px; padding: 16px 32px; background: #fff; border-bottom: 1px solid #e5e7eb;
}

.filter-btn {
  padding: 6px 16px; border: 1px solid #e5e7eb; border-radius: 20px;
  background: #fff; color: #6b7280; font-size: 13px; font-weight: 500; cursor: pointer;
  transition: all 0.15s;
}

.filter-btn:hover { border-color: #d1d5db; }
.filter-btn.active { background: #6366f1; color: #fff; border-color: #6366f1; }

.template-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(340px, 1fr));
  gap: 20px;
  padding: 28px 32px;
  max-width: 1200px;
}

.template-card {
  background: #fff; border: 1px solid #e5e7eb; border-radius: 14px;
  overflow: hidden; transition: all 0.2s;
  position: relative;
}

.template-card:hover {
  box-shadow: 0 8px 30px rgba(0,0,0,0.06);
  border-color: #d1d5db;
}

.tmpl-badge {
  position: absolute; top: 12px; right: 12px;
  padding: 3px 10px; border-radius: 10px;
  font-size: 11px; font-weight: 600; color: #fff;
}

.tmpl-badge.Dev { background: #3b82f6; }
.tmpl-badge.Analytics { background: #8b5cf6; }
.tmpl-badge.Writing { background: #f59e0b; color: #78350f; }
.tmpl-badge.Translate { background: #10b981; }
.tmpl-badge.Planning { background: #ef4444; }

.tmpl-content { padding: 22px; }

.tmpl-content h3 { font-size: 16px; font-weight: 700; color: #1f2937; margin: 0 0 8px; padding-right: 60px; }
.tmpl-content p { font-size: 13px; color: #6b7280; line-height: 1.5; margin: 0 0 14px; }

.tmpl-meta {
  display: flex; gap: 14px; font-size: 12px; color: #9ca3af;
  padding-bottom: 12px; border-bottom: 1px solid #f3f4f6; margin-bottom: 10px;
}

.tmpl-author { font-size: 12px; color: #d1d5db; margin-bottom: 14px; }

.use-btn {
  display: flex; align-items: center; justify-content: center; gap: 8px;
  width: 100%; padding: 10px;
  border: 1.5px solid #6366f1; border-radius: 10px;
  background: transparent; color: #6366f1;
  font-size: 14px; font-weight: 600; cursor: pointer;
  transition: all 0.15s;
}

.use-btn:hover { background: #6366f1; color: #fff; }

/* Modal */
.modal-overlay {
  position: fixed; inset: 0; background: rgba(0,0,0,0.4); backdrop-filter: blur(4px);
  display: flex; align-items: center; justify-content: center; z-index: 1000;
}

.modal-card {
  background: #fff; border-radius: 16px; width: 460px; max-width: 90vw;
  box-shadow: 0 20px 60px rgba(0,0,0,0.15);
}

.modal-header {
  display: flex; align-items: center; justify-content: space-between;
  padding: 16px 20px; border-bottom: 1px solid #e5e7eb;
  font-weight: 600; font-size: 15px;
}

.modal-close { background: none; border: none; cursor: pointer; color: #9ca3af; padding: 4px; }
.modal-close:hover { color: #374151; }

.modal-body { padding: 20px; }

.modal-desc { font-size: 14px; color: #6b7280; line-height: 1.6; margin: 0 0 20px; }

.modal-actions { display: flex; justify-content: flex-end; gap: 10px; }

.btn-cancel {
  padding: 10px 20px; border: 1px solid #d1d5db; border-radius: 10px;
  background: #fff; font-size: 14px; font-weight: 500; cursor: pointer;
}

.btn-confirm {
  display: flex; align-items: center; gap: 8px;
  padding: 10px 20px; border: none; border-radius: 10px;
  background: #6366f1; color: #fff; font-size: 14px; font-weight: 600; cursor: pointer;
}

.btn-confirm:hover { background: #4f46e5; }
</style>
