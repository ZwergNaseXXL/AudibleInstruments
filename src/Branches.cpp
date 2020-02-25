#include "AudibleInstruments.hpp"
#include "LongPressButton.hpp"

struct Branches : Module {
	enum ParamIds {
		THRESHOLD1_PARAM,
		THRESHOLD2_PARAM,
		MODE1_PARAM,
		MODE2_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		IN1_INPUT,
		IN2_INPUT,
		P1_INPUT,
		P2_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUT1A_OUTPUT,
		OUT2A_OUTPUT,
		OUT1B_OUTPUT,
		OUT2B_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		STATE1_POS_LIGHT, STATE1_NEG_LIGHT,
		STATE2_POS_LIGHT, STATE2_NEG_LIGHT,
		NUM_LIGHTS
	};

	enum TossMode { DIRECT, TOGGLE };
	enum OutMode { GATE, LATCH, THROUGH };

	dsp::SchmittTrigger gateTriggers[2];
	LongPressButton modeButtons[2];
	TossMode tossModes[2] = {TossMode::DIRECT, TossMode::DIRECT};
	OutMode outModes[2] = {OutMode::GATE, OutMode::GATE};
	bool tossResults[2] = {false, false};

	Branches() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(THRESHOLD1_PARAM, 0.0, 1.0, 0.5, "Probability 1");
		configParam(MODE1_PARAM, 0.0, 1.0, 0.0, "Mode 1");
		configParam(THRESHOLD2_PARAM, 0.0, 1.0, 0.5, "Probability 2");
		configParam(MODE2_PARAM, 0.0, 1.0, 0.0, "Mode 2");
	}

	template<typename M>
	void modeToJson(json_t *rootJ, char const *key, M const (&modes)[2]) {
		json_t *modesJ = json_array();
		for(int i = 0; i < 2; i++) {
			json_array_insert_new(modesJ, i, json_integer(modes[i]));
		}
		json_object_set_new(rootJ, key, modesJ);
	}

	json_t *dataToJson() override {
		json_t *rootJ = json_object();
		modeToJson(rootJ, "tossModes", tossModes);
		modeToJson(rootJ, "outModes", outModes);
		return rootJ;
	}

	template<typename M>
	void modeFromJson(json_t *rootJ, char const *key, M (&modes)[2], int max) {
		json_t *modesJ = json_object_get(rootJ, key);
		if(modesJ) {
			for(int i = 0; i < 2; i++) {
				json_t *modeJ = json_array_get(modesJ, i);
				if(modeJ && json_is_integer(modeJ)) {
					modes[i] = static_cast<M>(clamp(json_integer_value(modeJ), 0, max));
				}
			}
		}
	}

	void dataFromJson(json_t *rootJ) override {
		json_t *modesJ = json_object_get(rootJ, "modes");
		if(modesJ) { // legacy load -> analog through output mode
			for(int i = 0; i < 2; i++) {
				json_t *modeJ = json_array_get(modesJ, i);
				if(modeJ) {
					tossModes[i] = json_boolean_value(modeJ) ? TossMode::TOGGLE : TossMode::DIRECT;
				}
				outModes[i] = OutMode::THROUGH;
			}
		} else {
			modeFromJson(rootJ, "tossModes", tossModes, 1);
			modeFromJson(rootJ, "outModes", outModes, 2);
		}
	}

	template<typename M>
	void cycleMode(M & mode, int end) {
		mode = static_cast<M>((mode + 1) % end);
	}

	void process(const ProcessArgs &args) override {
		float input = 0.0f;
		for (int i = 0; i < 2; i++) {
			switch (modeButtons[i].step(params[MODE1_PARAM + i])) {
				case LongPressButton::SHORT_PRESS: cycleMode(tossModes[i], 2); break;
				case LongPressButton::LONG_PRESS: cycleMode(outModes[i], 3); break;
				default: break;
			}

			if (inputs[IN1_INPUT + i].isConnected()) {
				input = inputs[IN1_INPUT + i].getVoltage();
			}
			bool prevState = gateTriggers[i].isHigh();
			bool rising = gateTriggers[i].process(input);
			bool newState = gateTriggers[i].isHigh();
			bool falling = prevState && !newState;

			if (rising) {
				float r = random::uniform();
				float threshold = clamp(params[THRESHOLD1_PARAM + i].getValue() + inputs[P1_INPUT + i].getVoltage() / 10.f, 0.f, 1.f);
				bool toss = (r < threshold);
				if (tossModes[i] == TossMode::DIRECT) {
					tossResults[i] = toss;
				} else { // toggle
					tossResults[i] = (tossResults[i] != toss);
				}

				if(outModes[i] == OutMode::GATE || outModes[i] == OutMode::LATCH) { // open the gate
					outputs[OUT1A_OUTPUT + i].setVoltage(tossResults[i] ? 0.0f : 10.0f);
					outputs[OUT1B_OUTPUT + i].setVoltage(tossResults[i] ? 10.0f : 0.0f);
				}

				if(tossResults[i]) {
					lights[STATE1_NEG_LIGHT + 2 * i].setBrightness(1.0f);
				} else {
					lights[STATE1_POS_LIGHT + 2 * i].setBrightness(1.0f);
				}
				if (outModes[i] == OutMode::LATCH) {
					if(tossResults[i]) {
						lights[STATE1_POS_LIGHT + 2 * i].setBrightness(0.0f);
					} else {
						lights[STATE1_NEG_LIGHT + 2 * i].setBrightness(0.0f);
					}
				}
			} else if (falling) {
				if(outModes[i] == OutMode::GATE) { // close the gate
					outputs[OUT1A_OUTPUT + i].setVoltage(0.0f);
					outputs[OUT1B_OUTPUT + i].setVoltage(0.0f);
				}
			}

			if(outModes[i] == OutMode::THROUGH) { // analog through mode
				outputs[OUT1A_OUTPUT + i].setVoltage(tossResults[i] ? 0.0f : input);
				outputs[OUT1B_OUTPUT + i].setVoltage(tossResults[i] ? input : 0.0f);
			}

			if (!(newState || outModes[i] == OutMode::LATCH)) {
				lights[STATE1_POS_LIGHT + 2 * i].setSmoothBrightness(0.0f, args.sampleTime);
				lights[STATE1_NEG_LIGHT + 2 * i].setSmoothBrightness(0.0f, args.sampleTime);
			}
		}
	}

	void onReset() override {
		for(int i = 0; i < 2; i++) {
			tossModes[i] = TossMode::DIRECT;
			outModes[i] = OutMode::GATE;
			tossResults[i] = false;
		}
	}
};


struct BranchesWidget : ModuleWidget {
	BranchesWidget(Branches *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/Branches.svg")));

		addChild(createWidget<ScrewSilver>(Vec(15, 0)));
		addChild(createWidget<ScrewSilver>(Vec(15, 365)));

		addParam(createParam<Rogan1PSRed>(Vec(24, 64), module, Branches::THRESHOLD1_PARAM));
		addParam(createParam<TL1105>(Vec(69, 58), module, Branches::MODE1_PARAM));
		addInput(createInput<PJ301MPort>(Vec(9, 122), module, Branches::IN1_INPUT));
		addInput(createInput<PJ301MPort>(Vec(55, 122), module, Branches::P1_INPUT));
		addOutput(createOutput<PJ301MPort>(Vec(9, 160), module, Branches::OUT1A_OUTPUT));
		addOutput(createOutput<PJ301MPort>(Vec(55, 160), module, Branches::OUT1B_OUTPUT));

		addParam(createParam<Rogan1PSGreen>(Vec(24, 220), module, Branches::THRESHOLD2_PARAM));
		addParam(createParam<TL1105>(Vec(69, 214), module, Branches::MODE2_PARAM));
		addInput(createInput<PJ301MPort>(Vec(9, 278), module, Branches::IN2_INPUT));
		addInput(createInput<PJ301MPort>(Vec(55, 278), module, Branches::P2_INPUT));
		addOutput(createOutput<PJ301MPort>(Vec(9, 316), module, Branches::OUT2A_OUTPUT));
		addOutput(createOutput<PJ301MPort>(Vec(55, 316), module, Branches::OUT2B_OUTPUT));

		addChild(createLight<SmallLight<GreenRedLight>>(Vec(41.5, 169), module, Branches::STATE1_POS_LIGHT));
		addChild(createLight<SmallLight<GreenRedLight>>(Vec(41.5, 325), module, Branches::STATE2_POS_LIGHT));
	}

	template<typename M>
	struct ModeItem : MenuItem {
		M *target;
		M mode;
		void onAction(const event::Action &e) override {
			*target = mode;
		}
		void step() override {
			rightText = CHECKMARK(*target == mode);
			MenuItem::step();
		}
	};

	void appendContextMenu(Menu *menu) override {
		Branches *branches = dynamic_cast<Branches*>(module);
		assert(branches);

		struct TossModeSub : MenuItem {
			Branches *branches;
			int channel;
			Menu* createChildMenu() override {
				Menu *menu = new Menu();
				using TossModeItem = ModeItem<Branches::TossMode>;
				menu->addChild(construct<TossModeItem>(&MenuItem::text, "Direct", &TossModeItem::target, &branches->tossModes[channel], &TossModeItem::mode, Branches::TossMode::DIRECT));
				menu->addChild(construct<TossModeItem>(&MenuItem::text, "Toggle", &TossModeItem::target, &branches->tossModes[channel], &TossModeItem::mode, Branches::TossMode::TOGGLE));
				return menu;
			}
		};

		struct OutModeSub : MenuItem {
			Branches *branches;
			int channel;
			Menu* createChildMenu() override {
				Menu *menu = new Menu();
				using OutModeItem = ModeItem<Branches::OutMode>;
				menu->addChild(construct<OutModeItem>(&MenuItem::text, "Gate", &OutModeItem::target, &branches->outModes[channel], &OutModeItem::mode, Branches::OutMode::GATE));
				menu->addChild(construct<OutModeItem>(&MenuItem::text, "Latch", &OutModeItem::target, &branches->outModes[channel], &OutModeItem::mode, Branches::OutMode::LATCH));
				menu->addChild(construct<OutModeItem>(&MenuItem::text, "Through", &OutModeItem::target, &branches->outModes[channel], &OutModeItem::mode, Branches::OutMode::THROUGH));
				return menu;
			}
		};

		menu->addChild(construct<MenuLabel>());
		menu->addChild(construct<TossModeSub>(&MenuItem::text, "Channel 1 Toss Mode", &TossModeSub::branches, branches, &TossModeSub::channel, 0));
		menu->addChild(construct<OutModeSub>(&MenuItem::text, "Channel 1 Output Mode", &OutModeSub::branches, branches, &OutModeSub::channel, 0));
		menu->addChild(construct<TossModeSub>(&MenuItem::text, "Channel 2 Toss Mode", &TossModeSub::branches, branches, &TossModeSub::channel, 1));
		menu->addChild(construct<OutModeSub>(&MenuItem::text, "Channel 2 Output Mode", &OutModeSub::branches, branches, &OutModeSub::channel, 1));
	}
};


Model *modelBranches = createModel<Branches, BranchesWidget>("BranchesX");
