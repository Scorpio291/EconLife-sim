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
  reputation: {
    business: number;
    political: number;
    social: number;
    street: number;
  };
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

export interface Business {
  id: number;
  sector: string;
  province_id: number;
  cash: number;
  revenue_per_tick: number;
  cost_per_tick: number;
  output_quality: number;
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
  metrics: Metrics;
}

// Messages from the simulation
export type SimMessage =
  | { type: 'state'; state: SimState }
  | { type: 'ack'; success: boolean }
  | { type: 'error'; message: string };

// Speed modes
export type Speed = 'paused' | 'step' | 'play' | 'fast';
