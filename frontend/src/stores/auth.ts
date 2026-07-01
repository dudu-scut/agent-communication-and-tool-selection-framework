import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import { login as apiLogin, register as apiRegister, setAuthTokenGetter } from '../services/grpc-client'

export const useAuthStore = defineStore('auth', () => {
  const userId = ref<string | null>(null)
  const username = ref<string | null>(null)
  const token = ref<string | null>(null)
  const expiresAt = ref<number>(0)

  const isAuthenticated = computed(() => {
    if (!token.value) return false
    if (Date.now() > expiresAt.value) {
      logout()
      return false
    }
    return true
  })

  function setAuth(data: { user_id: string; username: string; token: string; expires_at: number }) {
    userId.value = data.user_id
    username.value = data.username
    token.value = data.token
    expiresAt.value = data.expires_at * 1000 // server returns seconds, JS uses ms
  }

  function clearAuth() {
    userId.value = null
    username.value = null
    token.value = null
    expiresAt.value = 0
  }

  async function login(user: string, pass: string): Promise<string | null> {
    try {
      const resp = await apiLogin(user, pass)
      if (resp.status.code !== 0) {
        return resp.status.message || 'Login failed'
      }
      setAuth(resp)
      return null // null = success
    } catch (err: any) {
      return err.message || 'Network error'
    }
  }

  async function register(user: string, pass: string, displayName = ''): Promise<string | null> {
    try {
      const resp = await apiRegister(user, pass, displayName)
      if (resp.status.code !== 0) {
        return resp.status.message || 'Registration failed'
      }
      return null // null = success, user still needs to login
    } catch (err: any) {
      return err.message || 'Network error'
    }
  }

  function logout() {
    clearAuth()
  }

  // Wire token getter so all gRPC calls include auth header
  setAuthTokenGetter(() => token.value)

  return {
    userId,
    username,
    token,
    isAuthenticated,
    login,
    register,
    logout,
  }
})
