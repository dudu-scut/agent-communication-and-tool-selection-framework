<template>
  <div class="share-view">
    <div class="share-content">
      <div class="share-header">
        <div>
          <h1>
            <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polygon points="12 2 22 8.5 22 15.5 12 22 2 15.5 2 8.5 12 2"/></svg>
            NexusAI Shared Session
          </h1>
          <div class="share-meta">
            <span>会话分享功能</span>
          </div>
        </div>
      </div>

      <GlassCard variant="highlight" padding="lg">
        <EmptyState
          icon="mdi:flask-outline"
          title="会话分享功能开发中"
          description="会话分享功能正在开发中，敬请期待"
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

.share-view {
  min-height: 100vh;
  background: var(--bg-primary);
}

.share-content {
  max-width: 800px;
  margin: 0 auto;
  padding: 32px 24px;
}

.share-header {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  margin-bottom: 32px;
  padding-bottom: 20px;
  border-bottom: 1px solid var(--border-default);
}

.share-header h1 {
  display: flex;
  align-items: center;
  gap: 10px;
  font-size: 20px;
  font-weight: 700;
  color: var(--text-primary);
  margin: 0 0 8px;
}

.share-meta {
  font-size: 13px;
  color: var(--text-tertiary);
  display: flex;
  gap: 8px;
}
</style>
