import { useSimStore } from '../store';

function formatActionType(type: string): string {
  return type.replace(/_/g, ' ');
}

function formatPayload(actionType: string, payload: Record<string, unknown>): string {
  switch (actionType) {
    case 'travel':
      return `Province ${payload['destination_province_id'] ?? '?'}`;
    case 'scene_card_choice':
      return `Card ${payload['scene_card_id'] ?? '?'}, Choice ${payload['choice_id'] ?? '?'}`;
    case 'calendar_commit':
      return payload['accept'] ? 'Accepted' : 'Declined';
    case 'start_business':
      return String(payload['sector'] ?? 'retail').replace(/_/g, ' ');
    case 'initiate_contact':
      return `NPC ${payload['target_npc_id'] ?? '?'}`;
    default:
      return JSON.stringify(payload);
  }
}

export function ActionHistoryPanel({ onClose }: { onClose: () => void }) {
  const actionLog = useSimStore((s) => s.actionLog);

  return (
    <div className="action-history-panel">
      <div className="action-history-header">
        <h3>Action Log</h3>
        <button className="close-btn" onClick={onClose}>&times;</button>
      </div>
      {actionLog.length === 0 ? (
        <p className="empty-msg">No actions taken yet</p>
      ) : (
        <div className="action-history-list">
          {[...actionLog].reverse().map((entry) => (
            <div key={entry.id} className="action-entry">
              <div className="action-entry-header">
                <span className="action-entry-type">{formatActionType(entry.actionType)}</span>
                <span>{entry.date}</span>
              </div>
              <div className="action-entry-detail">
                {formatPayload(entry.actionType, entry.payload)}
              </div>
            </div>
          ))}
        </div>
      )}
    </div>
  );
}
