<script setup lang="ts">
withDefaults(defineProps<{
  count?: number
}>(), {
  count: 6,
})
</script>

<template>
  <div class="page-loader">
    <div class="skeleton-header">
      <div class="skeleton skeleton-title"></div>
      <div class="skeleton skeleton-subtitle"></div>
    </div>
    <div class="skeleton-grid">
      <div
        v-for="i in count"
        :key="i"
        class="skeleton skeleton-card"
      ></div>
    </div>
  </div>
</template>

<style scoped>
@import "../../styles/design-tokens.css";

.page-loader {
  padding: var(--space-8);
  width: 100%;
}

.skeleton-header {
  margin-bottom: var(--space-8);
  display: flex;
  flex-direction: column;
  gap: var(--space-3);
}

.skeleton {
  background: var(--bg-elevated);
  background-image: linear-gradient(
    90deg,
    var(--bg-elevated) 0%,
    rgba(255, 255, 255, 0.06) 50%,
    var(--bg-elevated) 100%
  );
  background-size: 200% 100%;
  animation: shimmer 1.8s ease-in-out infinite;
  border-radius: var(--radius-md);
}

.skeleton-title {
  width: 220px;
  height: 28px;
}

.skeleton-subtitle {
  width: 160px;
  height: 16px;
  opacity: 0.6;
}

.skeleton-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));
  gap: var(--space-6);
}

.skeleton-card {
  height: 160px;
  border-radius: var(--radius-lg);
}

@keyframes shimmer {
  0% {
    background-position: -200% 0;
  }
  100% {
    background-position: 200% 0;
  }
}
</style>
