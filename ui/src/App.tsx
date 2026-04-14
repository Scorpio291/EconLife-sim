import { useEffect } from 'react';
import { useSimStore } from './store';
import { TopBar } from './components/TopBar';
import { SceneCardPanel } from './components/SceneCardPanel';
import { CalendarPanel } from './components/CalendarPanel';
import { ProvincePanel } from './components/ProvincePanel';
import { MetricsDashboard } from './components/MetricsDashboard';
import { BusinessPanel } from './components/BusinessPanel';

export function App() {
  const connected = useSimStore((s) => s.connected);
  const state = useSimStore((s) => s.state);
  const connect = useSimStore((s) => s.connect);

  useEffect(() => {
    connect();
  }, [connect]);

  if (!connected || !state) {
    return (
      <div className="app-loading">
        <div className="loading-spinner" />
        <p>Connecting to simulation...</p>
      </div>
    );
  }

  const hasSceneCards = state.pending_scene_cards.filter(c => c.chosen_choice_id === 0).length > 0;

  return (
    <div className="app">
      <TopBar />
      <div className="app-body">
        <main className="main-panel">
          {hasSceneCards ? (
            <SceneCardPanel />
          ) : (
            <MetricsDashboard />
          )}
          <BusinessPanel />
        </main>
        <aside className="side-panel">
          <CalendarPanel />
          <ProvincePanel />
        </aside>
      </div>
    </div>
  );
}
