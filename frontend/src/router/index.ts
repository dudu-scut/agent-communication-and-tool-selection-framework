import { createRouter, createWebHistory } from 'vue-router'
import { useAuthStore } from '../stores/auth'

const router = createRouter({
  history: createWebHistory(import.meta.env.BASE_URL),
  routes: [
    {
      path: '/login',
      name: 'login',
      component: () => import('../views/LoginView.vue'),
      meta: { public: true },
    },
    {
      path: '/',
      name: 'chat',
      component: () => import('../views/ChatView.vue'),
    },
    {
      path: '/admin',
      name: 'admin',
      component: () => import('../views/AdminView.vue'),
    },
    {
      path: '/sandbox',
      name: 'sandbox',
      component: () => import('../views/AgentSandbox.vue'),
    },
    {
      path: '/compare',
      name: 'compare',
      component: () => import('../views/CompareView.vue'),
    },
    {
      path: '/share/:shareId',
      name: 'share',
      component: () => import('../views/ShareView.vue'),
      meta: { public: true },
    },
    {
      path: '/templates',
      name: 'templates',
      component: () => import('../views/TemplateMarket.vue'),
    },
    {
      path: '/:pathMatch(.*)*',
      redirect: '/login',
    },
  ],
})

router.beforeEach((to) => {
  if (to.meta.public) {
    const auth = useAuthStore()
    if (to.name === 'login' && auth.isAuthenticated) {
      return { name: 'chat' }
    }
    return true
  }

  const auth = useAuthStore()
  if (!auth.isAuthenticated) {
    if (auth.token) auth.logout()
    return { name: 'login' }
  }
  return true
})

export default router
