import { useSimStore } from '../store';

export function CalendarPanel() {
  const state = useSimStore((s) => s.state);
  const sendAction = useSimStore((s) => s.sendAction);

  if (!state) return null;

  // Show only upcoming entries (start_tick >= current tick) sorted by start_tick
  const entries = [...state.calendar]
    .filter(e => e.start_tick >= state.tick)
    .sort((a, b) => a.start_tick - b.start_tick)
    .slice(0, 20);

  return (
    <div className="calendar-panel">
      <h3>Calendar</h3>
      {entries.length === 0 ? (
        <p className="empty-msg">No upcoming entries</p>
      ) : (
        <div className="calendar-list">
          {entries.map((entry) => (
            <div key={entry.id} className={`calendar-entry ${entry.mandatory ? 'mandatory' : ''}`}>
              <div className="calendar-entry-header">
                <span className={`entry-type-badge type-${entry.type}`}>
                  {entry.type}
                </span>
                <span className="entry-date">{entry.start_date}</span>
              </div>
              <div className="calendar-entry-body">
                {entry.npc_id > 0 && (
                  <span className="entry-npc">{entry.npc_name}</span>
                )}
                <span className="entry-duration">{entry.duration_ticks}d</span>
              </div>
              <div className="calendar-entry-actions">
                {!entry.player_committed ? (
                  <>
                    <button
                      className="cal-btn accept"
                      onClick={() => sendAction('calendar_commit', {
                        calendar_entry_id: entry.id,
                        accept: true,
                      })}
                    >
                      Accept
                    </button>
                    {!entry.mandatory && (
                      <button
                        className="cal-btn decline"
                        onClick={() => sendAction('calendar_commit', {
                          calendar_entry_id: entry.id,
                          accept: false,
                        })}
                      >
                        Decline
                      </button>
                    )}
                  </>
                ) : (
                  <span className="committed-badge">Committed</span>
                )}
              </div>
            </div>
          ))}
        </div>
      )}
    </div>
  );
}
