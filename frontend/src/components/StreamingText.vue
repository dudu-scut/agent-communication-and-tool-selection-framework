<template>
  <span class="streaming-text">
    {{ text }}<span v-if="text" class="cursor">|</span>
  </span>
</template>

<script setup lang="ts">
defineProps<{
  text: string
}>()
</script>

<style scoped>
.streaming-text {
  position: relative;
}

.cursor {
  animation: blink 0.8s step-end infinite;
  font-weight: 300;
  color: #6b7280;
}

@keyframes blink {
  50% { opacity: 0; }
}
</style>
