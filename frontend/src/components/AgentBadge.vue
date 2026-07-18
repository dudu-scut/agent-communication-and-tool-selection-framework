<template>
  <span class="agent-badge">
    {{ name }}
  </span>
</template>

<script setup lang="ts">
defineProps<{
  name: string
}>()
</script>

<style scoped>
@import "../styles/design-tokens.css";

.agent-badge {
  display: inline-block;
  padding: 2px 10px;
  border-radius: var(--radius-full);
  font-size: 12px;
  font-weight: 500;
  background: rgba(139, 92, 246, 0.15);
  color: var(--brand-secondary);
  border: 1px solid var(--border-brand);
}
</style>
