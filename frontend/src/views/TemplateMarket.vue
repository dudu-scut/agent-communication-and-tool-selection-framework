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

    <div class="template-grid">
      <GlassCard variant="highlight" padding="lg" class="empty-card">
        <EmptyState
          icon="mdi:flask-outline"
          title="模板市场开发中"
          description="预设模板市场功能正在开发中，敬请期待"
        />
      </GlassCard>
    </div>
  </div>
</template>

<script setup lang="ts">
import GlassCard from '../components/layout/GlassCard.vue'
import EmptyState from '../components/feedback/EmptyState.vue'
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

.template-header h1 { font-size: 22px; font-weight: 700; margin: 0 0 2px;
  background: linear-gradient(135deg, var(--color-warning), var(--color-error));
  -webkit-background-clip: text; -webkit-text-fill-color: transparent; background-clip: text;
}

.subtitle { font-size: 13px; color: var(--text-tertiary); }

.template-grid {
  display: grid;
  grid-template-columns: 1fr;
  gap: 20px;
  padding: 28px 32px;
  max-width: 1200px;
}

.empty-card {
  grid-column: 1 / -1;
}


/* Responsive */
@media (max-width: 768px) {
  .template-header {
    padding: 16px 20px;
  }
  .template-grid {
    padding: 16px 20px;
    gap: 16px;
  }
}

@media (max-width: 480px) {
  .template-header h1 {
    font-size: 18px;
  }
}
</style>
