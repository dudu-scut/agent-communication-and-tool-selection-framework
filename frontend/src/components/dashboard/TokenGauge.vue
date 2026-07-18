<script setup lang="ts">
import { computed } from 'vue'
import VChart from 'vue-echarts'
import { use } from 'echarts/core'
import { CanvasRenderer } from 'echarts/renderers'
import { GaugeChart } from 'echarts/charts'
import { TitleComponent } from 'echarts/components'

use([CanvasRenderer, GaugeChart, TitleComponent])

interface Props {
  used: number
  total: number
  label?: string
}

const props = withDefaults(defineProps<Props>(), {
  label: 'Token使用情况'
})

const percentage = computed(() => Math.round((props.used / props.total) * 100))

const gaugeOption = computed(() => ({
  series: [
    {
      type: 'gauge',
      startAngle: 220,
      endAngle: -40,
      min: 0,
      max: 100,
      splitNumber: 10,
      pointer: { show: false },
      progress: {
        show: true,
        overlap: false,
        roundCap: true,
        clip: false,
        itemStyle: {
          color: {
            type: 'linear',
            x: 0,
            y: 0,
            x2: 1,
            y2: 0,
            colorStops: [
              { offset: 0, color: '#10b981' },
              { offset: 0.5, color: '#f59e0b' },
              { offset: 1, color: '#ef4444' }
            ]
          }
        }
      },
      axisLine: {
        lineStyle: {
          width: 20,
          color: [[1, 'rgba(255, 255, 255, 0.08)']]
        }
      },
      splitLine: { show: false },
      axisTick: { show: false },
      axisLabel: { show: false },
      data: [{ value: percentage.value }],
      title: {
        fontSize: 14,
        color: '#94a3b8',
        offsetCenter: [0, '70%']
      },
      detail: {
        fontSize: 32,
        fontWeight: 'bold',
        color: '#f1f5f9',
        offsetCenter: [0, '20%'],
        formatter: '{value}%',
        valueAnimation: true
      }
    }
  ]
}))
</script>

<template>
  <div class="token-gauge">
    <VChart :option="gaugeOption" autoresize style="height: 200px;" />
    <div class="gauge-footer">
      <span class="used">{{ used.toLocaleString() }}</span>
      <span class="separator">/</span>
      <span class="total">{{ total.toLocaleString() }}</span>
    </div>
  </div>
</template>

<style scoped>
@import "../../styles/design-tokens.css";

.token-gauge {
  display: flex;
  flex-direction: column;
  align-items: center;
}

.gauge-footer {
  margin-top: var(--space-2);
  font-size: 14px;
  color: var(--text-secondary);
}

.gauge-footer .used {
  color: var(--text-primary);
  font-weight: 600;
}

.gauge-footer .separator {
  margin: 0 4px;
  color: var(--text-tertiary);
}

.gauge-footer .total {
  color: var(--text-secondary);
}
</style>
