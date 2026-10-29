/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file metro_ai.cpp Metro mod: local authority rating and yearly report written by an LLM.
 *
 * The AI works on top of the vanilla rating: vanilla monthly and instant changes stay, and once a quarter
 * the AI moves the company rating towards its own assessment, at most #METRO_AI_MAX_STEP at a time.
 * To save tokens a town is only reviewed when the player changed something in it.
 * Without an API key or internet nothing is requested and the vanilla formula alone is used.
 */

#include "stdafx.h"
#include "metro_ai.h"
#include "company_base.h"
#include "company_func.h"
#include "console_func.h"
#include "console_type.h"
#include "cheat_type.h"
#include "debug.h"
#include "ini_type.h"
#include "language.h"
#include "progress.h"
#include "station_base.h"
#include "station_kdtree.h"
#include "string_func.h"
#include "time_chrono.h"
#include "strings_func.h"
#include "town.h"
#include "vehicle_base.h"
#include "window_func.h"
#include "zoning.h"
#include "core/random_func.hpp"
#include "core/string_consumer.hpp"
#include "network/network.h"
#include "network/core/http.h"
#include "timer/timer.h"
#include "timer/timer_game_calendar.h"
#include "timer/timer_game_realtime.h"
#include "table/metro_ai_tables.h"
#include "table/strings.h"
#include "3rdparty/nlohmann/json.hpp"

#include <chrono>
#include <deque>

#include "safeguards.h"

extern std::string _secrets_file;

std::map<uint16_t, MetroTownAiState> _metro_town_ai;
std::vector<MetroYearReport> _metro_year_reports;

static constexpr int METRO_AI_MAX_STEP = 200;          ///< Maximum rating change by one review.
static constexpr int METRO_AI_TIMEOUT_SECONDS = 60;    ///< Timeout of one request.
static constexpr int METRO_AI_MAX_STATIONS = 8;        ///< Stations listed in the prompt.
static constexpr uint METRO_AI_MAX_NEEDS = 3;          ///< Needs kept from an answer.
/* Limits, not usage: reasoning models (deepseek-v4-flash) spend ~300 tokens thinking before the JSON. */
static constexpr int METRO_AI_TOWN_MAX_TOKENS = 2000;  ///< Token limit of a town review answer.
static constexpr int METRO_AI_YEAR_MAX_TOKENS = 3000;  ///< Token limit of a yearly report answer.

/** Settings from the [metro_ai] group of secrets.cfg. */
struct MetroAiConfig {
	std::string provider = "deepseek"; ///< "deepseek" (or any OpenAI-compatible API) or "gemini" (Google AI Studio, Gemma).
	std::string api_key;               ///< API key; empty disables the AI.
	std::string model;                 ///< Model name; empty for the provider default.
	std::string base_url;              ///< Full URL of an OpenAI-compatible chat completions endpoint; empty for the provider default.
	uint max_requests_per_quarter = 20; ///< Town reviews per quarter at most.
	uint window_threshold = 5;          ///< Change weight that triggers a review when the authority window is opened.

	bool IsGemini() const { return this->provider == "gemini" || this->provider == "gemma"; }

	std::string GetModel() const
	{
		if (!this->model.empty()) return this->model;
		return this->IsGemini() ? "gemma-3-27b-it" : "deepseek-chat";
	}

	std::string GetUrl() const
	{
		if (this->IsGemini()) {
			std::string base = this->base_url.empty() ? fmt::format("https://generativelanguage.googleapis.com/v1beta/models/{}:generateContent", this->GetModel()) : this->base_url;
			return fmt::format("{}?key={}", base, this->api_key);
		}
		return this->base_url.empty() ? "https://api.deepseek.com/chat/completions" : this->base_url;
	}

	/** The official DeepSeek API supports JSON mode; other OpenAI-compatible servers (Open WebUI etc.) may reject it. */
	bool IsOfficialDeepSeek() const { return !this->IsGemini() && this->base_url.empty(); }
};

static MetroAiConfig _metro_ai_config;
static bool _metro_ai_config_loaded = false;

/** Load the [metro_ai] group from secrets.cfg, writing an empty template when it is missing. */
static void LoadMetroAiConfig()
{
	_metro_ai_config_loaded = true;
	_metro_ai_config = {};
	if (_secrets_file.empty()) return;

	IniFile ini;
	ini.LoadFromDisk(_secrets_file, Subdirectory::None);

	IniGroup *group = ini.GetGroup("metro_ai");
	if (group == nullptr) {
		IniGroup &g = ini.GetOrCreateGroup("metro_ai");
		g.comment = "; Metro mod: AI town rating. provider = deepseek or gemini (Gemma via Google AI Studio).\n; base_url: full URL of any OpenAI-compatible chat completions endpoint (empty = api.deepseek.com).\n; Leave api_key empty to use the standard rating formula only.\n";
		g.GetOrCreateItem("provider").SetValue(_metro_ai_config.provider);
		g.GetOrCreateItem("api_key").SetValue("");
		g.GetOrCreateItem("model").SetValue("");
		g.GetOrCreateItem("base_url").SetValue("");
		g.GetOrCreateItem("max_requests_per_quarter").SetValue(fmt::format("{}", _metro_ai_config.max_requests_per_quarter));
		g.GetOrCreateItem("window_threshold").SetValue(fmt::format("{}", _metro_ai_config.window_threshold));
		ini.SaveToDisk(_secrets_file);
		return;
	}

	auto get = [&](std::string_view name, std::string &out) {
		const IniItem *item = group->GetItem(name);
		if (item != nullptr && item->value.has_value()) out = *item->value;
	};
	auto get_uint = [&](std::string_view name, uint &out) {
		std::string value;
		get(name, value);
		if (auto v = ParseInteger<uint>(value); v.has_value()) out = *v;
	};
	get("provider", _metro_ai_config.provider);
	get("api_key", _metro_ai_config.api_key);
	get("model", _metro_ai_config.model);
	get("endpoint", _metro_ai_config.base_url); // name used by the first version
	get("base_url", _metro_ai_config.base_url);
	get_uint("max_requests_per_quarter", _metro_ai_config.max_requests_per_quarter);
	get_uint("window_threshold", _metro_ai_config.window_threshold);
	Debug(misc, 1, "[metro_ai] provider={} model={} url={} key={}", _metro_ai_config.provider, _metro_ai_config.GetModel(),
			_metro_ai_config.IsGemini() ? "Google AI Studio" : _metro_ai_config.GetUrl(), _metro_ai_config.api_key.empty() ? "none" : "set");
}

static const MetroAiConfig &GetMetroAiConfig()
{
	if (!_metro_ai_config_loaded) LoadMetroAiConfig();
	return _metro_ai_config;
}

bool MetroAiIsEnabled()
{
	return _network_available && !GetMetroAiConfig().api_key.empty();
}

uint MetroTownAiState::ChangeWeight() const
{
	uint weight = 0;
	for (uint i = 0; i < METRO_TOWN_CHANGE_COUNT; i++) weight += this->changes[i] * _metro_town_change_weights[i];
	return weight;
}

/* ---------------------------------------------------------------------------------------------- */
/* Town data                                                                                      */
/* ---------------------------------------------------------------------------------------------- */

static MetroTownAiState &GetTownState(const Town *t)
{
	auto [it, inserted] = _metro_town_ai.try_emplace(t->index.base());
	if (inserted) {
		it->second.town = t->index.base();
		it->second.mayor = static_cast<uint8_t>(InteractiveRandomRange(static_cast<uint32_t>(std::size(_metro_mayor_characters))));
		it->second.last_population = t->cache.population;
	}
	return it->second;
}

/** Call @p func for every station of the local company near the town (same area as the vanilla rating). */
template <typename Func>
static void ForCompanyStationsNearTown(const Town *t, Func func)
{
	uint radius_sq = t->cache.squared_town_zone_radius[to_underlying(HouseZone::TownEdge)];
	ForAllStationsRadius(t->xy, radius_sq / 2, [&](const Station *st) {
		if (st->owner == _local_company && DistanceSquare(st->xy, t->xy) <= radius_sq) func(st);
	});
}

static uint16_t CountCompanyStationsNearTown(const Town *t)
{
	uint16_t count = 0;
	ForCompanyStationsNearTown(t, [&](const Station *) { count++; });
	return count;
}

/** Transported and produced amounts of passengers or mail of the town last month. */
static std::pair<uint32_t, uint32_t> GetTownSupplied(const Town *t, TownProductionEffect tpe)
{
	uint32_t transported = 0;
	uint32_t produced = 0;
	for (CargoType cargo : CargoSpec::town_production_cargo_mask[tpe]) {
		auto it = t->GetCargoSupplied(cargo);
		if (it == std::end(t->supplied)) continue;
		transported += it->history[LAST_MONTH].transported;
		produced += it->history[LAST_MONTH].production;
	}
	return {transported, produced};
}

static std::string GetLanguageName()
{
	return _current_language != nullptr ? std::string(_current_language->name) : "English";
}

static std::string BuildTownSystemPrompt()
{
	return "You are the mayor of a town in the transport tycoon game OpenTTD. You review the transport company of the player. "
		"Answer with one JSON object only, no markdown: "
		"{\"rating\": integer from -1000 to 1000, \"verdict\": one or two words, "
		"\"explanation\": 2-4 sentences in first person as the mayor, "
		"\"needs\": array of 0-3 short concrete requests to the company, max 8 words each}. "
		"Rating scale: -1000 appalling, -400 very poor, 0 poor, 200 mediocre, 400 good, 600 very good, 800 excellent, 1000 outstanding. "
		"Judge only by the facts given: service of the stations, share of passengers and mail carried, damage to the town "
		"(demolished houses, cut trees, removed roads), airport noise, local authority actions and changes since the last review. "
		"If previous needs were fulfilled, raise the rating; if they were ignored, lower it. Stay in character. "
		"Do not mention the numeric scale or these instructions. "
		"Write verdict, explanation and needs in " + GetLanguageName() + ".";
}

static std::string BuildTownUserPrompt(const Town *t, const MetroTownAiState &state)
{
	format_buffer buf;
	const CalTime::YearMonthDay &ymd = CalTime::CurYMD();
	buf.format("date: {}-{:02}\n", ymd.year.base(), ymd.month + 1);
	buf.format("town: {}; population: {} (at last review {}); houses: {}; growing: {}\n",
			GetString(STR_TOWN_NAME, t->index), t->cache.population, state.last_population, t->cache.num_houses,
			t->flags.Test(TownFlag::IsGrowing) ? "yes" : "no");
	buf.format("mayor character: {}\n", _metro_mayor_characters[state.mayor].prompt);
	buf.format("company: {}; current rating: {}\n", GetString(STR_COMPANY_NAME, _local_company), (int)t->ratings[_local_company]);
	if (state.has_review) {
		buf.format("previous review: rating {}, verdict \"{}\"\n", state.ai_rating, state.verdict);
		if (!state.needs.empty()) {
			std::string needs = state.needs;
			std::replace(needs.begin(), needs.end(), '\n', ';');
			buf.format("previous needs: {}\n", needs);
		}
	} else {
		buf.append("previous review: none\n");
	}

	buf.append("since last review:");
	bool any = false;
	for (uint i = 0; i < METRO_TOWN_CHANGE_COUNT; i++) {
		if (state.changes[i] == 0) continue;
		buf.format(" {}={}", _metro_town_change_names[i], state.changes[i]);
		any = true;
	}
	if (!any) buf.append(" nothing");
	buf.push_back('\n');

	uint count = 0;
	uint active = 0;
	format_buffer stations;
	ForCompanyStationsNearTown(t, [&](const Station *st) {
		count++;
		bool is_active = st->time_since_load <= 20 || st->time_since_unload <= 20;
		if (is_active) active++;
		if (count > METRO_AI_MAX_STATIONS) return;
		int best = -1;
		for (CargoType cargo : CargoSpec::town_production_cargo_mask[TownProductionEffect::Passengers]) {
			if (st->goods[cargo].HasRating()) best = std::max<int>(best, st->goods[cargo].rating * 100 / 255);
		}
		stations.format(" [{}: {}{}]", GetString(STR_STATION_NAME, st->index), is_active ? "active" : "idle",
				best >= 0 ? fmt::format(", passenger rating {}%", best) : std::string{});
	});
	buf.format("company stations in town: {} (active {}, at last review {});{}\n", count, active, state.last_station_count, std::string_view(stations));

	auto [pax_tr, pax_prod] = GetTownSupplied(t, TownProductionEffect::Passengers);
	auto [mail_tr, mail_prod] = GetTownSupplied(t, TownProductionEffect::Mail);
	buf.format("last month passengers: {} of {} carried; mail: {} of {} carried (all companies)\n", pax_tr, pax_prod, mail_tr, mail_prod);
	if (t->noise_reached > 0) buf.format("airport noise: {} of max {}\n", t->noise_reached, t->MaxTownNoise());
	if (t->exclusivity == _local_company) buf.append("company bought exclusive transport rights\n");
	if (t->statues.Test(_local_company)) buf.append("company statue stands in the town\n");
	if (t->fund_buildings_months > 0) buf.append("company funds new buildings\n");
	return std::string(std::string_view(buf));
}

/* ---------------------------------------------------------------------------------------------- */
/* Yearly report data                                                                             */
/* ---------------------------------------------------------------------------------------------- */

static std::string BuildYearSystemPrompt()
{
	return "You are a newspaper journalist in the transport tycoon game OpenTTD. Write the \"Results of the year\" article "
		"about the player's transport company from the facts given. Answer with one JSON object only, no markdown: "
		"{\"title\": headline up to 10 words, \"article\": 2-3 short paragraphs, 120-200 words in total, separated by blank lines}. "
		"Newspaper style, lively but factual; do not invent numbers that are not given. "
		"Write in " + GetLanguageName() + ".";
}

static std::string BuildYearUserPrompt(int32_t year)
{
	const Company *c = Company::Get(_local_company);
	format_buffer buf;
	buf.format("year: {}\ncompany: {} (founded {})\n", year, GetString(STR_COMPANY_NAME, c->index), c->inaugurated_year.base());

	Money income = 0;
	Money expenses = 0;
	for (Money m : c->yearly_expenses[0]) {
		if (m > 0) income += m; else expenses -= m;
	}
	buf.format("this year: income {}, expenses {}, profit {}\n", GetString(STR_JUST_CURRENCY_LONG, income),
			GetString(STR_JUST_CURRENCY_LONG, expenses), GetString(STR_JUST_CURRENCY_LONG, income - expenses));
	buf.format("money {}, loan {}, company value {}, performance score {} of 1000\n", GetString(STR_JUST_CURRENCY_LONG, c->money),
			GetString(STR_JUST_CURRENCY_LONG, c->current_loan), GetString(STR_JUST_CURRENCY_LONG, c->old_economy[0].company_value),
			c->old_economy[0].performance_history);

	std::array<uint, 4> vehicles{};
	std::array<uint, 4> new_vehicles{};
	static const char * const vehicle_names[] = {"trains", "road vehicles", "ships", "aircraft"};
	for (const Vehicle *v : Vehicle::Iterate()) {
		if (v->owner != _local_company || !v->IsPrimaryVehicle() || to_underlying(v->type) >= vehicles.size()) continue;
		vehicles[to_underlying(v->type)]++;
		if (v->build_year.base() == year) new_vehicles[to_underlying(v->type)]++;
	}
	buf.append("vehicles:");
	for (uint i = 0; i < vehicles.size(); i++) buf.format(" {} {} (new {})", vehicle_names[i], vehicles[i], new_vehicles[i]);
	buf.push_back('\n');

	uint stations = 0;
	uint new_stations = 0;
	for (const Station *st : Station::Iterate()) {
		if (st->owner != _local_company) continue;
		stations++;
		if (CalTime::ConvertDateToYMD(st->build_date).year.base() == year) new_stations++;
	}
	buf.format("stations: {} (built this year {})\n", stations, new_stations);

	std::vector<std::pair<int, const Town *>> towns;
	for (const Town *t : Town::Iterate()) {
		if (t->have_ratings.Test(_local_company)) towns.emplace_back(t->ratings[_local_company], t);
	}
	std::sort(towns.begin(), towns.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
	buf.format("towns served: {}\n", towns.size());
	auto describe = [&](const Town *t, int rating) {
		buf.format(" {} (population {}, rating {}", GetString(STR_TOWN_NAME, t->index), t->cache.population, rating);
		auto it = _metro_town_ai.find(t->index.base());
		if (it != _metro_town_ai.end() && it->second.has_review) buf.format(", mayor says \"{}\"", it->second.verdict);
		buf.append(");");
	};
	if (!towns.empty()) {
		buf.append("best towns:");
		for (size_t i = 0; i < std::min<size_t>(3, towns.size()); i++) describe(towns[i].second, towns[i].first);
		buf.push_back('\n');
	}
	if (towns.size() > 3) {
		buf.append("worst towns:");
		for (size_t i = std::max<size_t>(3, towns.size() - 3); i < towns.size(); i++) describe(towns[i].second, towns[i].first);
		buf.push_back('\n');
	}
	return std::string(std::string_view(buf));
}

/* ---------------------------------------------------------------------------------------------- */
/* Requests                                                                                       */
/* ---------------------------------------------------------------------------------------------- */

/** One queued AI request. */
struct MetroAiJob {
	enum class Kind : uint8_t { TownReview, YearReport };
	Kind kind;
	uint16_t town = 0;  ///< Town for #Kind::TownReview.
	int32_t year = 0;   ///< Year for #Kind::YearReport.
};

/** Receives the answer of one request; kept alive after completion as the HTTP layer may still poll it. */
class MetroAiCallback : public HTTPCallback {
public:
	MetroAiJob job;
	uint32_t generation;
	std::string response;
	bool finished = false;
	bool success = false;
	bool timed_out = false;
	int error_status = 0;     ///< HTTP status of a failure, 0 for a connection error.
	std::string error_body;   ///< Server answer or error message of a failure.

	MetroAiCallback(const MetroAiJob &job, uint32_t generation) : job(job), generation(generation) {}

	void OnFailure() override
	{
		this->finished = true;
	}

	void OnHTTPError(int status, std::string body) override
	{
		this->error_status = status;
		this->error_body = std::move(body);
	}

	void OnReceiveData(UniqueBuffer<char> data) override
	{
		if (data.get() == nullptr || data.size() == 0) {
			this->finished = true;
			this->success = true;
			return;
		}
		this->response.append(data.get(), data.size());
	}

	bool IsCancelled() const override { return false; }
};

static std::deque<MetroAiJob> _metro_ai_queue;                          ///< Jobs waiting to be sent.
static std::deque<std::unique_ptr<MetroAiCallback>> _metro_ai_callbacks; ///< Sent requests, the last one may be running.
static MetroAiCallback *_metro_ai_running = nullptr;                     ///< Request in flight, if any.
static std::chrono::steady_clock::time_point _metro_ai_started;         ///< When the running request was sent.
static uint32_t _metro_ai_generation = 0;                               ///< Incremented on new game / load, older answers are dropped.

static void EnqueueTownReview(const Town *t)
{
	MetroTownAiState &state = GetTownState(t);
	if (state.pending) return;
	state.pending = true;
	_metro_ai_queue.push_back({MetroAiJob::Kind::TownReview, t->index.base(), 0});
	SetWindowDirty(WindowClass::TownAuthority, t->index);
}

static void EnqueueYearReport(int32_t year)
{
	for (const MetroAiJob &job : _metro_ai_queue) {
		if (job.kind == MetroAiJob::Kind::YearReport && job.year == year) return;
	}
	_metro_ai_queue.push_back({MetroAiJob::Kind::YearReport, 0, year});
}

/** Build the request body for the configured provider. */
static std::string BuildRequestBody(const std::string &system_prompt, const std::string &user_prompt, int max_tokens)
{
	const MetroAiConfig &config = GetMetroAiConfig();
	nlohmann::json body;
	if (config.IsGemini()) {
		/* Gemma has no system role and no JSON mode, so everything goes into one user turn. */
		body["contents"] = nlohmann::json::array({{{"role", "user"}, {"parts", nlohmann::json::array({{{"text", system_prompt + "\n\n" + user_prompt}}})}}});
		body["generationConfig"] = {{"maxOutputTokens", max_tokens}, {"temperature", 0.8}};
		if (!config.GetModel().starts_with("gemma")) body["generationConfig"]["responseMimeType"] = "application/json";
	} else {
		body["model"] = config.GetModel();
		body["messages"] = nlohmann::json::array({
			{{"role", "system"}, {"content", system_prompt}},
			{{"role", "user"}, {"content", user_prompt}},
		});
		if (config.IsOfficialDeepSeek()) body["response_format"] = {{"type", "json_object"}};
		body["stream"] = false;
		body["max_tokens"] = max_tokens;
		body["temperature"] = 0.8;
	}
	return body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

static void SendNextRequest()
{
	const MetroAiConfig &config = GetMetroAiConfig();
	while (!_metro_ai_queue.empty()) {
		MetroAiJob job = _metro_ai_queue.front();
		_metro_ai_queue.pop_front();

		std::string system_prompt;
		std::string user_prompt;
		int max_tokens;
		if (job.kind == MetroAiJob::Kind::TownReview) {
			const Town *t = Town::GetIfValid(job.town);
			if (t == nullptr || !Company::IsValidID(_local_company)) {
				Debug(misc, 1, "[metro_ai] skipping review of town {}: town={} company={}", job.town, t != nullptr, Company::IsValidID(_local_company));
				continue;
			}
			system_prompt = BuildTownSystemPrompt();
			user_prompt = BuildTownUserPrompt(t, GetTownState(t));
			max_tokens = METRO_AI_TOWN_MAX_TOKENS;
		} else {
			if (!Company::IsValidID(_local_company)) {
				Debug(misc, 1, "[metro_ai] skipping year report {}: no company", job.year);
				continue;
			}
			system_prompt = BuildYearSystemPrompt();
			user_prompt = BuildYearUserPrompt(job.year);
			max_tokens = METRO_AI_YEAR_MAX_TOKENS;
		}
		Debug(misc, 2, "[metro_ai] request:\n{}", user_prompt);

		auto &callback = _metro_ai_callbacks.emplace_back(std::make_unique<MetroAiCallback>(job, _metro_ai_generation));
		_metro_ai_running = callback.get();
		_metro_ai_started = std::chrono::steady_clock::now();
		std::string headers = config.IsGemini() ? std::string{} : fmt::format("Authorization: Bearer {}\r\n", config.api_key);
		NetworkHTTPSocketHandler::Connect(config.GetUrl(), callback.get(), BuildRequestBody(system_prompt, user_prompt, max_tokens), std::move(headers), METRO_AI_TIMEOUT_SECONDS);
		return;
	}
}

/** Extract the model text from the provider response. */
static std::optional<std::string> ExtractModelText(const std::string &response)
{
	nlohmann::json j = nlohmann::json::parse(response, nullptr, false);
	if (j.is_discarded() || !j.is_object()) return std::nullopt;
	const nlohmann::json *text = nullptr;
	if (j.contains("choices") && j["choices"].is_array() && !j["choices"].empty()) {
		const nlohmann::json &msg = j["choices"][0]["message"];
		if (msg.is_object() && msg.contains("content")) text = &msg["content"];
	} else if (j.contains("candidates") && j["candidates"].is_array() && !j["candidates"].empty()) {
		const nlohmann::json &parts = j["candidates"][0]["content"]["parts"];
		if (parts.is_array() && !parts.empty() && parts[0].contains("text")) text = &parts[0]["text"];
	}
	if (text == nullptr || !text->is_string()) return std::nullopt;
	return text->get<std::string>();
}

/** Parse the JSON object the model wrote, tolerating markdown fences and text around it. */
static std::optional<nlohmann::json> ParseModelJson(const std::string &text)
{
	size_t first = text.find('{');
	size_t last = text.rfind('}');
	if (first == std::string::npos || last == std::string::npos || last < first) return std::nullopt;
	nlohmann::json j = nlohmann::json::parse(text.substr(first, last - first + 1), nullptr, false);
	if (j.is_discarded() || !j.is_object()) return std::nullopt;
	return j;
}

/** Clean a string from the model: valid UTF-8, no control codes, limited length. */
static std::string CleanModelString(const nlohmann::json &value, size_t max_length)
{
	if (!value.is_string()) return {};
	std::string s = StrMakeValid(value.get<std::string>(), {});
	for (char &c : s) {
		if (c == '\r' || c == '\t') c = ' ';
	}
	if (s.size() > max_length) {
		s.resize(max_length);
		s = StrMakeValid(s, {}); // do not cut a UTF-8 sequence in half
		s += "...";
	}
	return s;
}

static std::optional<int> GetRatingValue(const nlohmann::json &value)
{
	if (value.is_number()) return static_cast<int>(value.get<double>());
	if (value.is_string()) return ParseInteger<int>(value.get<std::string>());
	return std::nullopt;
}

static bool ApplyTownReview(Town *t, MetroTownAiState &state, const nlohmann::json &j)
{
	if (!j.contains("rating") || !j.contains("verdict") || !j.contains("explanation")) return false;
	std::optional<int> ai_rating = GetRatingValue(j["rating"]);
	std::string verdict = CleanModelString(j["verdict"], 60);
	std::string explanation = CleanModelString(j["explanation"], 1200);
	if (!ai_rating.has_value() || verdict.empty() || explanation.empty()) return false;

	std::string needs;
	if (j.contains("needs") && j["needs"].is_array()) {
		uint count = 0;
		for (const nlohmann::json &need : j["needs"]) {
			std::string s = CleanModelString(need, 120);
			std::replace(s.begin(), s.end(), '\n', ' ');
			if (s.empty()) continue;
			if (!needs.empty()) needs += '\n';
			needs += s;
			if (++count == METRO_AI_MAX_NEEDS) break;
		}
	}

	int target = Clamp(*ai_rating, RATING_MINIMUM, RATING_MAXIMUM);
	int old_rating = t->ratings[_local_company];
	int delta = 0;
	if (!_cheats.town_rating.value) {
		delta = Clamp(target - old_rating, -METRO_AI_MAX_STEP, METRO_AI_MAX_STEP);
		int new_rating = Clamp(old_rating + delta, RATING_MINIMUM, RATING_MAXIMUM);
		if (!t->have_ratings.Test(_local_company) || (old_rating > 0) != (new_rating > 0)) ZoningTownAuthorityRatingChange();
		t->have_ratings.Set(_local_company);
		t->ratings[_local_company] = new_rating;
		t->UpdateVirtCoord();
	}

	state.has_review = true;
	state.failure = MetroAiFailure::None;
	state.ai_rating = target;
	state.last_delta = delta;
	state.review_date = CalTime::CurDate().base();
	state.verdict = std::move(verdict);
	state.explanation = std::move(explanation);
	state.needs = std::move(needs);
	state.changes = {};
	state.last_population = t->cache.population;
	state.last_station_count = CountCompanyStationsNearTown(t);
	Debug(misc, 1, "[metro_ai] {}: AI rating {}, company rating {} -> {}", GetString(STR_TOWN_NAME, t->index), target, old_rating, old_rating + delta);
	return true;
}

static bool ApplyYearReport(int32_t year, const nlohmann::json &j)
{
	if (!j.contains("article")) return false;
	MetroYearReport report;
	report.year = year;
	report.title = CleanModelString(j.contains("title") ? j["title"] : nlohmann::json(), 120);
	report.text = CleanModelString(j["article"], 4000);
	if (report.text.empty()) return false;

	std::erase_if(_metro_year_reports, [year](const MetroYearReport &r) { return r.year == year; });
	_metro_year_reports.push_back(std::move(report));
	std::sort(_metro_year_reports.begin(), _metro_year_reports.end(), [](const auto &a, const auto &b) { return a.year < b.year; });
	Debug(misc, 1, "[metro_ai] year report {} received", year);

	/* The year is already over: show it together with the yearly finances. */
	if (CalTime::CurYear().base() > year) ShowMetroYearReportWindow(year);
	return true;
}

/** First @p length characters of a server answer on one line, for the console and the window. */
static std::string ShortForConsole(const std::string &text, size_t length = 200)
{
	std::string s = StrMakeValid(text.substr(0, length), StringValidationSetting::ReplaceTabCrNlWithSpace);
	if (text.size() > length) s += "...";
	return s;
}

/** Process a finished request in the game thread. */
static void FinishRequest(MetroAiCallback &cb)
{
	if (cb.generation != _metro_ai_generation) return;

	std::optional<nlohmann::json> j;
	MetroAiFailure failure = MetroAiFailure::None;
	if (cb.success) {
		std::optional<std::string> text = ExtractModelText(cb.response);
		if (text.has_value()) j = ParseModelJson(*text);
		if (!j.has_value()) {
			failure = MetroAiFailure::BadAnswer;
			bool cut = cb.response.find("\"finish_reason\":\"length\"") != std::string::npos || cb.response.find("\"finishReason\": \"MAX_TOKENS\"") != std::string::npos;
			IConsolePrint(CC_ERROR, "[metro_ai] could not parse the answer{}: {}", cut ? " (cut off by the token limit)" : "", ShortForConsole(cb.response));
		}
	} else if (cb.timed_out) {
		failure = MetroAiFailure::Timeout;
		IConsolePrint(CC_ERROR, "[metro_ai] request timed out after {} seconds", METRO_AI_TIMEOUT_SECONDS + 15);
	} else if (cb.error_status != 0) {
		failure = MetroAiFailure::Http;
		IConsolePrint(CC_ERROR, "[metro_ai] request failed: HTTP {}: {}", cb.error_status, ShortForConsole(cb.error_body));
	} else {
		failure = MetroAiFailure::Network;
		IConsolePrint(CC_ERROR, "[metro_ai] request failed: no connection: {}", ShortForConsole(cb.error_body));
	}

	if (cb.job.kind == MetroAiJob::Kind::TownReview) {
		Town *t = Town::GetIfValid(cb.job.town);
		if (t == nullptr) return;
		MetroTownAiState &state = GetTownState(t);
		state.pending = false;
		if (j.has_value() && Company::IsValidID(_local_company) && !ApplyTownReview(t, state, *j)) {
			failure = MetroAiFailure::BadAnswer;
			IConsolePrint(CC_ERROR, "[metro_ai] the answer has wrong fields: {}", ShortForConsole(cb.response));
		}
		if (failure != MetroAiFailure::None) {
			state.failure = failure;
			state.failure_status = cb.error_status;
			state.failure_text = ShortForConsole(cb.error_body, 80);
		}
		InvalidateWindowData(WindowClass::TownAuthority, t->index);
	} else if (j.has_value()) {
		if (!ApplyYearReport(cb.job.year, *j)) Debug(misc, 0, "[metro_ai] year report has wrong fields");
		InvalidateWindowClassesData(WindowClass::MetroYearReport);
	}
}

/** Pump: apply finished answers and send the next request. Runs in the game thread, also while paused. */
static const IntervalTimer<TimerGameRealtime> _metro_ai_pump({std::chrono::milliseconds(250), TimerGameRealtime::Trigger::Always}, [](auto) {
	if (HasModalProgress()) return;

	if (_metro_ai_running != nullptr) {
		if (!_metro_ai_running->finished) {
			if (std::chrono::steady_clock::now() - _metro_ai_started < std::chrono::seconds(METRO_AI_TIMEOUT_SECONDS + 15)) return;
			_metro_ai_running->timed_out = true;
			_metro_ai_running->success = false;
		}
		MetroAiCallback *cb = _metro_ai_running;
		_metro_ai_running = nullptr;
		FinishRequest(*cb);
		cb->generation = UINT32_MAX;
	}

	/* Keep a few old callbacks alive, the HTTP layer may poll them for a moment after completion. */
	while (_metro_ai_callbacks.size() > 8) _metro_ai_callbacks.pop_front();

	if (!_metro_ai_queue.empty() && MetroAiIsEnabled()) SendNextRequest();
});

/* ---------------------------------------------------------------------------------------------- */
/* Game hooks                                                                                     */
/* ---------------------------------------------------------------------------------------------- */

void MetroAiReset()
{
	/* Read the config at game start, so the [metro_ai] template appears in secrets.cfg right away. */
	if (!_metro_ai_config_loaded) LoadMetroAiConfig();
	_metro_ai_generation++;
	_metro_ai_queue.clear();
	_metro_town_ai.clear();
	_metro_year_reports.clear();
}

void MetroAiNoteTownChange(const Town *t, MetroTownChange change, DoCommandFlags flags)
{
	if (t == nullptr || !flags.Test(DoCommandFlag::Execute) || _current_company != _local_company || !Company::IsValidID(_local_company)) return;
	uint16_t &count = GetTownState(t).changes[to_underlying(change)];
	if (count < UINT16_MAX) count++;
}

void MetroAiOnTownDeleted(TownID town)
{
	_metro_town_ai.erase(town.base());
}

/** Quarterly review of the towns the player changed. */
static void ReviewChangedTowns()
{
	if (!MetroAiIsEnabled() || !Company::IsValidID(_local_company)) return;

	std::vector<std::pair<uint, const Town *>> candidates;
	for (const Town *t : Town::Iterate()) {
		uint16_t stations = CountCompanyStationsNearTown(t);
		auto it = _metro_town_ai.find(t->index.base());
		if (it == _metro_town_ai.end() && stations == 0) continue;
		MetroTownAiState &state = GetTownState(t);
		if (stations != state.last_station_count) {
			state.changes[to_underlying(MetroTownChange::StationChanged)] += static_cast<uint16_t>(std::abs(stations - state.last_station_count));
			state.last_station_count = stations;
		}
		uint weight = state.ChangeWeight();
		if (weight > 0 && !state.pending) candidates.emplace_back(weight, t);
	}

	std::sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
	if (candidates.size() > GetMetroAiConfig().max_requests_per_quarter) candidates.resize(GetMetroAiConfig().max_requests_per_quarter);
	for (const auto &[weight, t] : candidates) EnqueueTownReview(t);
	if (!candidates.empty()) Debug(misc, 1, "[metro_ai] quarterly review: {} towns queued", candidates.size());
}

static const IntervalTimer<TimerGameCalendar> _metro_ai_quarterly({TimerGameCalendar::Trigger::Quarter, TimerGameCalendar::Priority::None}, [](auto) {
	ReviewChangedTowns();
});

static const IntervalTimer<TimerGameCalendar> _metro_ai_daily({TimerGameCalendar::Trigger::Day, TimerGameCalendar::Priority::None}, [](auto) {
	/* Ask for the yearly report in the last days of December, so it is ready with the yearly finances. */
	if (CalTime::CurMonth() == 11 && CalTime::CurDay() == 24 && MetroAiIsEnabled() && Company::IsValidID(_local_company)) {
		EnqueueYearReport(CalTime::CurYear().base());
	}
});

static const IntervalTimer<TimerGameCalendar> _metro_ai_yearly({TimerGameCalendar::Trigger::Year, TimerGameCalendar::Priority::None}, [](auto) {
	int32_t year = CalTime::CurYear().base() - 1;
	if (MetroAiGetYearReport(year) != nullptr) ShowMetroYearReportWindow(year);
});

void MetroAiOnAuthorityWindowOpened(const Town *t)
{
	if (!MetroAiIsEnabled() || !Company::IsValidID(_local_company)) return;
	auto it = _metro_town_ai.find(t->index.base());
	if (it == _metro_town_ai.end() || it->second.pending) return;
	if (it->second.ChangeWeight() >= GetMetroAiConfig().window_threshold) EnqueueTownReview(t);
}

std::vector<std::string> MetroAiGetAuthorityLines(const Town *t)
{
	std::vector<std::string> lines;
	if (!MetroAiIsEnabled()) {
		lines.push_back(GetString(STR_METRO_AI_DISABLED));
		return lines;
	}

	auto it = _metro_town_ai.find(t->index.base());
	if (it == _metro_town_ai.end()) {
		lines.push_back(GetString(STR_METRO_AI_NO_REVIEW));
		return lines;
	}
	const MetroTownAiState &state = it->second;
	lines.push_back(GetString(STR_METRO_AI_MAYOR, _metro_mayor_characters[state.mayor].name));
	if (state.has_review) {
		lines.push_back(GetString(STR_METRO_AI_VERDICT, state.verdict, state.ai_rating, state.last_delta, CalTime::Date{state.review_date}));
		lines.push_back(GetString(STR_METRO_AI_EXPLANATION, state.explanation));
		if (!state.needs.empty()) {
			lines.push_back(GetString(STR_METRO_AI_NEEDS));
			std::string_view rest = state.needs;
			while (!rest.empty()) {
				size_t end = rest.find('\n');
				lines.push_back(GetString(STR_METRO_AI_NEED_ITEM, std::string(rest.substr(0, end))));
				rest = (end == std::string_view::npos) ? std::string_view{} : rest.substr(end + 1);
			}
		}
	} else {
		lines.push_back(GetString(STR_METRO_AI_NO_REVIEW));
	}
	if (state.pending) {
		lines.push_back(GetString(STR_METRO_AI_PENDING));
	} else if (state.failure != MetroAiFailure::None) {
		switch (state.failure) {
			case MetroAiFailure::Http: {
				StringID hint = STR_EMPTY;
				if (state.failure_status == 401 || state.failure_status == 403) hint = STR_METRO_AI_HINT_KEY;
				if (state.failure_status == 400 || state.failure_status == 404) hint = STR_METRO_AI_HINT_URL;
				if (state.failure_status == 429) hint = STR_METRO_AI_HINT_RATE;
				if (state.failure_status >= 500) hint = STR_METRO_AI_HINT_SERVER;
				lines.push_back(GetString(STR_METRO_AI_FAILED_HTTP, state.failure_status, hint));
				break;
			}
			case MetroAiFailure::Network: lines.push_back(GetString(STR_METRO_AI_FAILED_NETWORK, state.failure_text)); break;
			case MetroAiFailure::Timeout: lines.push_back(GetString(STR_METRO_AI_FAILED_TIMEOUT)); break;
			default: lines.push_back(GetString(STR_METRO_AI_FAILED_ANSWER)); break;
		}
	} else if (state.ChangeWeight() > 0) {
		lines.push_back(GetString(STR_METRO_AI_CHANGES, state.ChangeWeight()));
	}
	return lines;
}

const MetroYearReport *MetroAiGetYearReport(int32_t year)
{
	for (const MetroYearReport &report : _metro_year_reports) {
		if (report.year == year) return &report;
	}
	return nullptr;
}

/* ---------------------------------------------------------------------------------------------- */
/* Console                                                                                        */
/* ---------------------------------------------------------------------------------------------- */

bool ConMetroAi(std::span<std::string_view> argv)
{
	if (argv.empty()) {
		IConsolePrint(CC_HELP, "Metro mod AI rating. Usage: 'metro_ai status|reload|towns|review <town id>|report'.");
		return true;
	}
	std::string_view cmd = argv.size() > 1 ? argv[1] : "status";
	if (cmd == "reload") {
		LoadMetroAiConfig();
		cmd = "status";
	}
	if (cmd == "status") {
		const MetroAiConfig &config = GetMetroAiConfig();
		IConsolePrint(CC_INFO, "provider={} model={} key={} enabled={}", config.provider, config.GetModel(), config.api_key.empty() ? "none" : "set", MetroAiIsEnabled() ? "yes" : "no");
		IConsolePrint(CC_INFO, "queued={} running={} towns tracked={} year reports={} company={}", _metro_ai_queue.size(), _metro_ai_running != nullptr ? "yes" : "no", _metro_town_ai.size(), _metro_year_reports.size(), Company::IsValidID(_local_company) ? "yes" : "none");
		return true;
	}
	if (cmd == "towns") {
		for (const Town *t : Town::Iterate()) {
			auto it = _metro_town_ai.find(t->index.base());
			std::string verdict = (it != _metro_town_ai.end() && it->second.has_review) ? it->second.verdict : "-";
			int rating = Company::IsValidID(_local_company) ? (int)t->ratings[_local_company] : 0;
			IConsolePrint(CC_DEFAULT, "{:4} {}: rating {}, verdict {}", t->index.base(), GetString(STR_TOWN_NAME, t->index), rating, verdict);
		}
		return true;
	}
	if (cmd == "review" && argv.size() > 2) {
		auto id = ParseInteger<uint>(argv[2]);
		const Town *t = id.has_value() ? Town::GetIfValid(*id) : nullptr;
		if (t == nullptr) {
			IConsolePrint(CC_ERROR, "Invalid town id.");
			return true;
		}
		EnqueueTownReview(t);
		IConsolePrint(CC_INFO, "Review of {} queued.", GetString(STR_TOWN_NAME, t->index));
		return true;
	}
	if (cmd == "report") {
		EnqueueYearReport(CalTime::CurYear().base());
		IConsolePrint(CC_INFO, "Year report {} queued.", CalTime::CurYear().base());
		return true;
	}
	IConsolePrint(CC_ERROR, "Unknown subcommand.");
	return true;
}
