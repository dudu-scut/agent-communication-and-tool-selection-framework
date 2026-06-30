/**
 * gRPC-Web 客户端封装
 *
 * 通过 HTTP/1.1 与 grpcwebproxy 通信，后者再转为 gRPC/2 转发到 NexusAI。
 * 当前使用手写 TypeScript 类型 + fetch，后续可切换为 protoc 生成的 stub。
 */

import type {
  AIQueryRequest,
  AIQueryResponse,
  AIStreamEvent,
  GetAgentsResponse,
  FindAgentsRequest,
  FindAgentsResponse,
} from '../types/proto'

const BASE_URL = import.meta.env.VITE_API_BASE || ''

// AIQueryService 路径前缀
const AI_QUERY = '/agent_communication.AIQueryService'
const AGENT_COMM = '/agent_communication.AgentCommunicationService'

/**
 * 一元 RPC 调用（JSON 序列化，通过 grpcwebproxy 转发）
 */
async function unaryCall<TReq, TRes>(
  servicePath: string,
  method: string,
  request: TReq,
): Promise<TRes> {
  const url = `${BASE_URL}${servicePath}/${method}`

  const resp = await fetch(url, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(request),
  })

  if (!resp.ok) {
    throw new Error(`RPC ${method} failed: ${resp.status} ${resp.statusText}`)
  }

  return resp.json() as Promise<TRes>
}

// ============================================================================
// AIQueryService
// ============================================================================

/** 同步查询 */
export async function query(
  question: string,
  contextId?: string,
): Promise<AIQueryResponse> {
  const req: AIQueryRequest = {
    request_id: crypto.randomUUID(),
    question,
    context_id: contextId || 'default',
    history_length: 5,
    timeout_seconds: 60,
    metadata: {},
  }
  return unaryCall<AIQueryRequest, AIQueryResponse>(AI_QUERY, 'Query', req)
}

/**
 * 流式查询
 *
 * 使用 fetch + ReadableStream 消费 server-streaming 响应。
 * grpcwebproxy 返回的是 gRPC-Web text 格式（base64 编码帧），
 * 这里简化处理为逐行解析 JSON 事件。
 */
export function queryStream(
  question: string,
  onEvent: (event: AIStreamEvent) => void,
  contextId?: string,
  signal?: AbortSignal,
): void {
  const req: AIQueryRequest = {
    request_id: crypto.randomUUID(),
    question,
    context_id: contextId || 'default',
    history_length: 5,
    timeout_seconds: 120,
    metadata: {},
  }

  const url = `${BASE_URL}${AI_QUERY}/QueryStream`

  fetch(url, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(req),
    signal,
  })
    .then(async (resp) => {
      if (!resp.ok) {
        throw new Error(`QueryStream failed: ${resp.status}`)
      }

      const reader = resp.body?.getReader()
      if (!reader) throw new Error('No response body')

      const decoder = new TextDecoder()
      let buffer = ''

      while (true) {
        const { done, value } = await reader.read()
        if (done) break

        buffer += decoder.decode(value, { stream: true })
        // 按换行分割，处理 SSE 风格的帧
        const lines = buffer.split('\n')
        buffer = lines.pop() || '' // 最后一个可能不完整

        for (const line of lines) {
          const trimmed = line.trim()
          if (!trimmed || trimmed.startsWith(':')) continue
          // 尝试解析 data: 前缀（SSE 格式）或直接 JSON
          const jsonStr = trimmed.startsWith('data: ')
            ? trimmed.slice(6)
            : trimmed
          try {
            const event = JSON.parse(jsonStr) as AIStreamEvent
            onEvent(event)
          } catch {
            // 非 JSON 行，跳过
          }
        }
      }

      // 处理 buffer 中剩余内容
      if (buffer.trim()) {
        try {
          const event = JSON.parse(buffer.trim()) as AIStreamEvent
          onEvent(event)
        } catch {
          // ignore
        }
      }
    })
    .catch((err) => {
      if (err.name !== 'AbortError') {
        onEvent({
          event_id: '',
          event_type: 'error',
          content: err.message || 'Stream failed',
          task_state: 'failed',
          context_id: contextId || 'default',
          timestamp: Date.now(),
        })
      }
    })
}

// ============================================================================
// AgentCommunicationService
// ============================================================================

/** 获取所有已注册 Agent */
export async function getAgents(
  filter = '',
  limit = 100,
): Promise<GetAgentsResponse> {
  return unaryCall(AGENT_COMM, 'GetAgents', {
    filter,
    limit,
    offset: 0,
  })
}

/** 按 tag/skill/keyword 查找 Agent */
export async function findAgents(
  params: Partial<FindAgentsRequest>,
): Promise<FindAgentsResponse> {
  return unaryCall(AGENT_COMM, 'FindAgents', {
    tag: '',
    skill: '',
    keyword: '',
    limit: 100,
    ...params,
  })
}
