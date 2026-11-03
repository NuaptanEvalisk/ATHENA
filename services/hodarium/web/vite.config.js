import { defineConfig } from 'vite';

export default defineConfig({
  build: {
    rolldownOptions: {
      onLog(level, log, handler) {
        // This panel is entirely client-side; RSC client boundaries are redundant.
        if (log.code === 'MODULE_LEVEL_DIRECTIVE' && log.message.includes('"use client"')) return;
        handler(level, log);
      },
    },
  },
});
