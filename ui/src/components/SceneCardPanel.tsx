import { useSimStore } from '../store';

function toneColor(tone: number): string {
  // -1.0 (hostile/red) to 1.0 (warm/green)
  if (tone < -0.3) return 'var(--tone-hostile)';
  if (tone > 0.3) return 'var(--tone-warm)';
  return 'var(--tone-neutral)';
}

function presentationClass(state: number): string {
  if (state < 0.3) return 'npc-hostile';
  if (state > 0.7) return 'npc-cooperative';
  return 'npc-neutral';
}

export function SceneCardPanel() {
  const state = useSimStore((s) => s.state);
  const sendAction = useSimStore((s) => s.sendAction);

  if (!state) return null;

  const activeCards = state.pending_scene_cards.filter(c => c.chosen_choice_id === 0);

  if (activeCards.length === 0) return null;

  return (
    <div className="scene-card-panel">
      <h2>Scene Cards</h2>
      {activeCards.map((card) => (
        <div key={card.id} className={`scene-card ${presentationClass(card.npc_presentation_state)}`}>
          <div className="scene-card-header">
            <span className="scene-type">{card.type.replace(/_/g, ' ')}</span>
            <span className="scene-setting">{card.setting.replace(/_/g, ' ')}</span>
          </div>

          <div className="scene-card-npc">
            <span className="npc-name">{card.npc_name}</span>
          </div>

          {card.dialogue.length > 0 && (
            <div className="scene-dialogue">
              {card.dialogue.map((line, i) => (
                <div key={i} className="dialogue-line" style={{ borderLeftColor: toneColor(line.tone) }}>
                  <span className="dialogue-speaker">{line.speaker}</span>
                  <p className="dialogue-text">{line.text}</p>
                </div>
              ))}
            </div>
          )}

          <div className="scene-choices">
            {card.choices.map((choice) => (
              <button
                key={choice.id}
                className="choice-btn"
                onClick={() => sendAction('scene_card_choice', {
                  scene_card_id: card.id,
                  choice_id: choice.id,
                })}
                title={choice.description}
              >
                {choice.label}
              </button>
            ))}
          </div>
        </div>
      ))}
    </div>
  );
}
