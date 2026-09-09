// TypeScript interfaces mirroring the JSON state from the C++ interactive CLI.

export interface Player {
  id: number;
  wealth: number;
  health: number;
  exhaustion: number;
  age: number;
  province_id: number;
  home_province_id: number;
  travel_status: 'resident' | 'in_transit' | 'visiting';
  lifespan_projection: number;
  reputation: {
    business: number;
    political: number;
    social: number;
    street: number;
  };
  // Only domains the player has actually raised above the floor.
  skills: { domain: string; level: number; last_exercise_tick: number }[];
  // What the PLAYER knows is held against them — not what the world holds.
  evidence_known: {
    token_id: number;
    discovery_tick: number;
    type?: string;
    actionability?: number;
  }[];
}

export interface DialogueLine {
  speaker: string;
  text: string;
  tone: number; // -1.0 (hostile) to 1.0 (warm)
}

export interface PlayerChoice {
  id: number;
  label: string;
  description: string;
}

export interface SceneCard {
  id: number;
  type: 'meeting' | 'call' | 'personal_event' | 'news_notification';
  setting: string;
  npc_id: number;
  npc_name: string;
  npc_presentation_state: number; // 0.0 to 1.0
  chosen_choice_id: number;
  dialogue: DialogueLine[];
  choices: PlayerChoice[];
}

export interface CalendarEntry {
  id: number;
  start_tick: number;
  start_date: string;
  duration_ticks: number;
  type: 'meeting' | 'event' | 'operation' | 'deadline' | 'personal';
  npc_id: number;
  npc_name: string;
  player_committed: boolean;
  mandatory: boolean;
  scene_card_id: number;
}

export interface Province {
  id: number;
  name: string;
  population: number;
  infrastructure: number;
  stability: number;
  crime: number;
  grievance: number;
  cohesion: number;
}

export interface Facility {
  id: number;
  province_id: number;
  recipe_id: string;
  workers: number;
  max_workers: number;
  operational: boolean;
}

export interface Business {
  id: number;
  sector: string;
  province_id: number;
  cash: number;
  revenue_per_tick: number;
  cost_per_tick: number;
  profit_per_tick: number;
  output_quality: number;
  facilities: Facility[];
}

// A firm trading in the player's own province that they could buy. `fair_price`
// is the seller's yardstick — thirty days of takings at the fair multiple —
// which is what an offer is judged against, not a quote.
export interface AcquisitionTarget {
  id: number;
  sector: string;
  province_id: number;
  owner_npc_id: number;
  revenue_per_tick: number;
  cost_per_tick: number;
  fair_price: number;
}

export interface PendingAcquisition {
  id: number;
  business_id: number;
  price: number;
  offered_tick: number;
  close_tick: number;
  stage: string;
}

export interface Metrics {
  npc_count: number;
  business_count: number;
  avg_npc_capital: number;
  avg_spot_price: number;
}

export interface SimState {
  tick: number;
  date: string;
  player: Player;
  pending_scene_cards: SceneCard[];
  calendar: CalendarEntry[];
  provinces: Province[];
  businesses: Business[];
  acquisition_targets: AcquisitionTarget[];
  pending_acquisitions: PendingAcquisition[];
  metrics: Metrics;
}

// Messages from the simulation
export type SimMessage =
  | { type: 'state'; state: SimState }
  | { type: 'ack'; success: boolean }
  | { type: 'error'; message: string };

// Speed modes
export type Speed = 'paused' | 'step' | 'play' | 'fast';

export interface MetricsSnapshot {
  tick: number;
  avg_npc_capital: number;
  avg_spot_price: number;
  npc_count: number;
  business_count: number;
}

export interface ActionLogEntry {
  id: number;
  tick: number;
  date: string;
  actionType: string;
  payload: Record<string, unknown>;
  timestamp: number;
}

export interface ToastItem {
  id: number;
  message: string;
}
