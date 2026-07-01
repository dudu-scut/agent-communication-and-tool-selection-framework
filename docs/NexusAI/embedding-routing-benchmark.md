## Embedding 路由阈值调优与性能基准报告 (P3-3)

### 1. 三层路由管线架构

NexusAI 的技能路由采用四层渐近管线，从低成本到高成本逐级回退：

```
用户查询
  │
  ├─ Tier 0: LLM 意图分类（~800ms，最高精度）
  │    分析查询文本，返回精确技能名
  │    匹配成功 → 直接路由
  │
  ├─ Tier 1: Embedding 高置信度（~50ms，高精度）
  │    余弦相似度 > high_threshold (0.85)
  │    → 直接路由，不调 LLM
  │
  ├─ Tier 2: Embedding 模糊区（~50ms，需确认）
  │    余弦相似度在 low_threshold ~ high_threshold 之间
  │    → 返回候选，由调用方决定是否接受
  │
  └─ Tier 3: 关键词 IDF 倒排索引（~1ms，基础覆盖）
       Embedding 低于 low_threshold 或不可用
       → 基于词频权重的关键词匹配
```

每一层的命中直接返回，不再进入下一层。这意味着绝大多数查询在 Tier 0-1 就能解决，只有少量边界情况才需要降级到关键词匹配。

### 2. 阈值参数化配置

所有阈值通过 `EmbeddingRouterConfig` 结构体外部注入，不硬编码在路由逻辑中：

```cpp
struct EmbeddingRouterConfig {
    bool enabled = false;
    float high_threshold = 0.85f;   // 余弦相似度 > 此值 → 直接路由
    float low_threshold = 0.50f;    // 余弦相似度 < 此值 → 降级关键词
    std::string api_key;            // embedding API 密钥
    std::string model = "deepseek-v4-pro";
    int dimension = 1024;
    std::string api_url = "https://api.deepseek.com/v1/embeddings";
};
```

调用方通过 `AgentRouter::enableEmbedding(config)` 启用。`enableEmbedding()` 内部校验 `high_threshold > low_threshold`，不合法则拒绝启用。运行时阈值在 `analyzeRequiredSkillHybrid()` 中从 `embedding_config_` 读取，修改配置后重新调用 `enableEmbedding()` 即可生效。

### 3. 相似度度量与索引结构

**相似度计算**：余弦相似度（Cosine Similarity），非点积。实现位于 `VectorIndex::cosineSimilarity()`，对两个归一化向量计算点积后除以模长乘积。

**索引结构**：暴力线性扫描（Brute-force Linear Scan）。每次查询遍历全部已索引技能，计算余弦相似度，按降序排列后取 Top-K。对于当前规模（数十个技能），线性扫描的延迟可忽略不计（< 0.1ms），不需要引入 ANN 索引。

**Embedding 文本格式**：`"skill_name: description"`。将技能名和描述拼接后送入 Embedding API，让向量同时捕获名称和语义信息。

**缓存**：LRU + TTL（500 条上限，1 小时过期）。命中缓存时跳过 API 调用，直接将缓存向量送入相似度计算。

### 4. 延迟分析

各层延迟量级基于架构推算（不含网络传输）：

| 路由层 | 典型延迟 | 延迟构成 |
|--------|----------|----------|
| Tier 0 (LLM) | 600-1200ms | HTTP 请求 + LLM 推理（首 token 流式） |
| Tier 1 (Embedding 高置信) | 30-80ms | 缓存命中 ~0.5ms / API 调用 30-80ms |
| Tier 2 (Embedding 模糊) | 30-80ms | 同上，结果需调用方二次判断 |
| Tier 3 (关键词 IDF) | < 1ms | 内存倒排索引查找 + IDF 加权求和 |

**关键优化点**：

Embedding 缓存命中率是延迟的决定性因素。缓存命中时，一次路由查询仅需 ~1ms（向量计算 + 线性扫描），比 LLM 调用快两个数量级。缓存未命中时需要一次 Embedding API 调用（30-80ms），仍然比 LLM（600-1200ms）快一个数量级。

缓存容量 500 条覆盖大部分生产场景。对于重复查询模式（同一用户多次问类似问题），TTL 1 小时内缓存命中率高。

### 5. 成本分析

假设每天 10000 次路由查询，各层的成本模型：

| 路由层 | 单次成本 | 日调用量（估算分布） | 日成本 |
|--------|----------|---------------------|--------|
| Tier 0 (LLM) | ~¥0.01 | 20% = 2000 次 | ¥20 |
| Tier 1 (Embedding) | ~¥0.001 | 50% = 5000 次 | ¥5 |
| Tier 3 (关键词) | ¥0 | 30% = 3000 次 | ¥0 |
| **合计** | | | **¥25/天** |

对比纯 LLM 路由（每次查询都调 LLM）：10000 × ¥0.01 = ¥100/天。Embedding 快路径节省约 75% 的 LLM 调用成本。

成本节省比例取决于 Tier 1 命中率。当 Embedding 高置信度命中率达到 50% 以上时，Embedding 快路径的成本优势显著。阈值越低，Tier 1 命中率越高，但路由精度下降；阈值越高，精度越高但 LLM 调用增多。

### 6. 阈值调优建议

**默认阈值选择依据**：

`high_threshold = 0.85` 的选择基于余弦相似度的语义区分度。在 1024 维 embedding 空间中，0.85 以上的余弦相似度通常意味着查询和技能在语义上高度一致（如同义表达、细微改写）。0.85 以下开始出现语义漂移风险。

`low_threshold = 0.50` 是语义相关性的下限。低于 0.50 的匹配通常是噪声（如"天气查询"匹配到"代码审查"），不如直接走关键词。

**调优方向**：

- **技能数量少（< 10 个）**：可以提高 `high_threshold` 到 0.90。技能少意味着 embedding 空间更稀疏，高相似度更可靠。
- **技能数量多（> 50 个）**：可以降低 `high_threshold` 到 0.80。技能多时 embedding 空间更密，0.85 可能漏掉有效匹配。
- **技能描述详尽**：保持默认 0.85。描述越详细，embedding 质量越高，阈值不需要调整。
- **技能描述简略**：降低 `high_threshold` 到 0.75-0.80。描述不充分时 embedding 区分度下降。
- **查询风格多样**：保持或降低阈值。用户查询越多样，与技能描述的文本差异越大。
- **查询风格集中**：可以提高阈值。查询模式固定时，embedding 匹配更稳定。

**模糊区（0.50-0.85）的处理策略**：

当前实现中，模糊区匹配和直接匹配走同一路径（都加入 `skills_to_match` 列表用于 agent 筛选）。如果需要更精细的控制，可以在模糊区引入 LLM 二次确认——将候选技能名送回 LLM 做 yes/no 判断。这会增加 ~300ms 延迟但提高精度。

### 7. 已知限制与改进方向

**线性扫描的可扩展性**：当前索引用暴力线性扫描。技能数超过 500 时，单次查询延迟可能超过 1ms。此时应引入 HNSW 或 IVF 等 ANN 索引。不过对于 NexusAI 当前的应用场景（数十个 Agent、百级技能），线性扫描完全够用。

**Embedding API 可用性**：当 Embedding API 不可用时，`analyzeRequiredSkillHybrid()` 会 catch 异常并降级到关键词匹配。这是一个安全回退机制，但降级后路由精度会下降。生产环境应配置 API 监控和告警。

**缓存冷启动**：系统刚启动时缓存为空，前 500 次不同查询都需要调用 Embedding API。可以考虑预热策略：在 `enableEmbedding()` 时预先 embed 所有已注册技能（已在 `buildSkillEmbeddingIndex()` 中实现），但查询侧缓存需要运行时积累。

**阈值动态调整**：当前阈值是静态配置。一个潜在的改进方向是根据运行时统计（`embedding_hit_count_` / `embedding_query_count_`）自动微调阈值。命中率过低时自动降低 `high_threshold`，命中率高但误路由多时自动提高。这需要引入路由正确性的反馈机制。
