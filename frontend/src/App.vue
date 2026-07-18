<script setup lang="ts">
import { computed } from 'vue'
import { useRoute } from 'vue-router'
import AppLayout from './components/layout/AppLayout.vue'
import ToastNotification from './components/feedback/ToastNotification.vue'
import ProgressBar from './components/feedback/ProgressBar.vue'

const route = useRoute()
const noLayout = computed(() => {
  return route.path === '/login' || route.path.startsWith('/share/')
})
</script>

<template>
  <AppLayout v-if="!noLayout">
    <router-view v-slot="{ Component, route: r }">
      <Transition name="page" mode="out-in">
        <component :is="Component" :key="r.path" />
      </Transition>
    </router-view>
  </AppLayout>
  <router-view v-else />
  <ToastNotification />
  <ProgressBar />
</template>

<style scoped>
@import "./styles/design-tokens.css";

.page-enter-active,
.page-leave-active {
  transition: opacity var(--duration-normal) var(--ease-default),
              transform var(--duration-normal) var(--ease-default);
}

.page-enter-from {
  opacity: 0;
  transform: translateY(12px);
}

.page-leave-to {
  opacity: 0;
  transform: translateY(-12px);
}
</style>
