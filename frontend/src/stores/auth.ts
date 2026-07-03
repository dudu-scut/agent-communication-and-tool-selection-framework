import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import { login as apiLogin, register as apiRegister, setAuthTokenGetter, setOnUnauthorized } from '../services/grpc-client'
import { useChatStore } from './chat'
import { useAgentsStore } from './agents'

const AUTH_STORAGE_KEY = 'nexusai_auth'

// Periodic token expiry check — runs once, shared across store instances
let _expiryTimer: ReturnType<typeof setInterval> | null = null
let _expiryCheckFn: (() => void) | null = null

export const useAuthStore = defineStore('auth', () => {
  const userId = ref<string | null>(null)
  const username = ref<string | null>(null)
  const token = ref<string | null>(null)
  const expiresAt = ref<number>(0)

  // Hydrate from localStorage on store init
  try {
    const saved = localStorage.getItem(AUTH_STORAGE_KEY)
    if (saved) {
      const data = JSON.parse(saved)
      userId.value = data.userId ?? null
      username.value = data.username ?? null
      token.value = data.token ?? null
      expiresAt.value = data.expiresAt ?? 0
    }
  } catch {
    // ignore corrupt localStorage data
  }

  const isAuthenticated = computed(() => {
    if (!token.value) return false
    if (Date.now() > expiresAt.value) return false
    return true
  })

  function saveToStorage() {
    try {
      localStorage.setItem(AUTH_STORAGE_KEY, JSON.stringify({
        userId: userId.value,
        username: username.value,
        token: token.value,
        expiresAt: expiresAt.value,
      }))
    } catch {
      // localStorage full or disabled — auth still works in-memory
    }
  }

  function setAuth(data: { user_id: string; username: string; token: string; expires_at: number }) {
    userId.value = data.user_id
    username.value = data.username
    token.value = data.token
    expiresAt.value = data.expires_at * 1000 // server returns seconds, JS uses ms
    saveToStorage()
  }

  function clearAuth() {
    userId.value = null
    username.value = null
    token.value = null
    expiresAt.value = 0
    localStorage.removeItem(AUTH_STORAGE_KEY)
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
    useChatStore().newConversation()
    useAgentsStore().stopPolling()
  }

  // Wire token getter so all gRPC calls include auth header
  setAuthTokenGetter(() => token.value)

  // Wire unauthorized callback so 401 responses trigger logout
  // Only fire when user actually had a session (prevents 401 during login triggering logout)
  setOnUnauthorized(() => {
    if (token.value) logout()
  })

  // Periodic token expiry check — proactively logout when token expires
  // (Date.now() in computed isn't time-reactive, so we poll)
  _expiryCheckFn = () => {
    if (token.value && Date.now() > expiresAt.value) {
      logout()
    }
  }
  if (!_expiryTimer) {
    _expiryTimer = setInterval(() => _expiryCheckFn?.(), 30_000)
  }

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
