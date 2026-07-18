<script setup lang="ts">
import { computed, ref, onMounted, watch } from 'vue'
import { CountUp } from 'countup.js'
import VChart from 'vue-echarts'
import { use } from 'echarts/core'
import { CanvasRenderer } from 'echarts/renderers'
import { LineChart } from 'echarts/charts'
import { GridComponent, TooltipComponent } from 'echarts/components'

use([CanvasRenderer, LineChart, GridComponent, TooltipComponent])

interface TrendItem {
  date: string
  cost: number
}

interface AgentCost {
  name: string
  cost: number
}

interface Props {
  totalCost: number
  trend: TrendItem[]
  byAgent: AgentCost[]
}

const props = defineProps<Props>()

const counterRef = ref<HTMLElement | null>(null)
let countUp: CountUp | null = null

onMounted(() => {
  if (counterRef.value) {
    countUp = new CountUp(counterRef.value, props.totalCost, {
      duration: 2,
      decimalPlaces: 2,
      prefix: '$',
      separator: ',',
      useEasing: true
    })
    countUp.start()
  }
})

watch(() => props.totalCost, (newVal) => {
  if (countUp) {
    countUp.update(newVal)
  }
})

const chartOption = computed(() => ({
  backgroundColor: 'transparent',
  tooltip: {
    trigger: 'axis',
    backgroundColor: 'rgba(17, 24, 39, 0.95)',
    borderColor: 'rgba(255, 255, 255, 0.1)',
    textStyle: { color: '#f1f5f9', fontSize: 12 },
    formatter: (params: any) => {
      const p = params[0]
      return `${p.name}<br/>$${p.value.toFixed(2)}`
    }
  },
  grid: {
    left: '3%',
    right: '4%',
    bottom: '3%',
    top: '8%',
    containLabel: true
  },
  xAxis: {
    type: 'category',
    data: props.trend.map(t => t.date),
    axisLine: { lineStyle: { color: 'rgba(255, 255, 255, 0.1)' } },
    axisLabel: { color: '#94a3b8', fontSize: 11 },
    boundaryGap: false
  },
  yAxis: {
    type: 'value',
    axisLine: { show: false },
    axisLabel: {
      color: '#94a3b8',
      fontSize: 11,
      formatter: '${value}'
    },
    splitLine: { lineStyle: { color: 'rgba(255, 255, 255, 0.05)' } }
  },
  series: [
    {
      type: 'line',
      data: props.trend.map(t => t.cost),
      smooth: true,
      symbol: 'circle',
      symbolSize: 6,
      lineStyle: {
        color: '#6366f1',
        width: 3
      },
      itemStyle: { color: '#6366f1' },
      areaStyle: {
        color: {
          type: 'linear',
          x: 0,
          y: 0,
          x2: 0,
          y2: 1,
          colorStops: [
            { offset: 0, color: 'rgba(99, 102, 241, 0.3)' },
            { offset: 1, color: 'rgba(99, 102, 241, 0.05)' }
          ]
        }
      }
    }
  ]
}))
</script>

<template>
  <div class="cost-tracker">
    <div class="cost-header">
      <span class="cost-label">总成本</span>
      <span ref="counterRef" class="cost-value">$0.00</span>
    </div>

    <div class="cost-chart">
      <VChart :option="chartOption" autoresize style="height: 180px;" />
    </div>

    <div class="cost-table">
      <div class="table-header">
        <span>Agent</span>
        <span>成本</span>
      </div>
      <div
        v-for="agent in byAgent"
        :key="agent.name"
        class="table-row"
      >
        <span class="agent-name">{{ agent.name }}</span>
        <span class="agent-cost">${{ agent.cost.toFixed(2) }}</span>
      </div>
    </div>
  </div>
</template>

<style scoped>
@import "../../styles/design-tokens.css";

.cost-tracker {
  display: flex;
  flex-direction: column;
  gap: var(--space-4);
}

.cost-header {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: var(--space-2);
}

.cost-label {
  font-size: 14px;
  color: var(--text-secondary);
  text-transform: uppercase;
  letter-spacing: 0.05em;
}

.cost-value {
  font-size: 42px;
  font-weight: 700;
  color: var(--text-primary);
  font-family: var(--font-mono);
}

.cost-chart {
  width: 100%;
}

.cost-table {
  display: flex;
  flex-direction: column;
  gap: var(--space-2);
  padding: var(--space-4);
  background: var(--glass-bg);
  border: 1px solid var(--glass-border);
  border-radius: var(--radius-md);
}

.table-header {
  display: flex;
  justify-content: space-between;
  font-size: 12px;
  color: var(--text-tertiary);
  text-transform: uppercase;
  letter-spacing: 0.05em;
  padding-bottom: var(--space-2);
  border-bottom: 1px solid var(--border-subtle);
}

.table-row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: var(--space-2) 0;
}

.agent-name {
  font-size: 14px;
  color: var(--text-primary);
}

.agent-cost {
  font-size: 14px;
  font-weight: 600;
  color: var(--text-primary);
  font-family: var(--font-mono);
}
</style>
