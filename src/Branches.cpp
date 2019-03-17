#include "AudibleInstruments.hpp"
#include "LongPressButton.hpp"
#include "dsp/digital.hpp"


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
		TOSS_MODE1_LIGHT,
		TOSS_MODE2_LIGHT,
		OUT_MODE1_POS_LIGHT, OUT_MODE1_NEG_LIGHT,
		OUT_MODE2_POS_LIGHT, OUT_MODE2_NEG_LIGHT,
		STATE1_POS_LIGHT, STATE1_NEG_LIGHT,
		STATE2_POS_LIGHT, STATE2_NEG_LIGHT,
		NUM_LIGHTS
	};

	enum TossMode { DIRECT, TOGGLE };
	enum OutMode { GATE, LATCH, THROUGH };

	SchmittTrigger gateTriggers[2];
	LongPressButton modeButtons[2];
	TossMode tossModes[2] = {TossMode::DIRECT, TossMode::DIRECT};
	OutMode outModes[2] = {OutMode::GATE, OutMode::GATE};
	bool tossResults[2] = {false, false};

	Branches() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {}

	template<typename E>
	void toJson(json_t * rootJ, char const * key, E const (&modes)[2]) {
		json_t * modesJ = json_array();
		for(int i = 0; i < 2; i++) {
			json_array_insert_new(modesJ, i, json_integer(modes[i]));
		}
		json_object_set_new(rootJ, key, modesJ);
	}

	json_t *toJson() override {
		json_t * rootJ = json_object();
		toJson(rootJ, "tossModes", tossModes);
		toJson(rootJ, "outModes", outModes);
		return rootJ;
	}

	template<typename E>
	void fromJson(json_t * rootJ, char const * key, E (&modes)[2], int max) {
		json_t * modesJ = json_object_get(rootJ, key);
		if(modesJ) {
			for(int i = 0; i < 2; i++) {
				json_t * modeJ = json_array_get(modesJ, i);
				if(modeJ && json_is_integer(modeJ)) {
					modes[i] = static_cast<E>(clamp(json_integer_value(modeJ), 0, max));
				}
			}
		}
	}

	void fromJson(json_t * rootJ) override {
		json_t * modesJ = json_object_get(rootJ, "modes");
		if(modesJ) { // legacy load -> analog through output mode
			for(int i = 0; i < 2; i++) {
				json_t * modeJ = json_array_get(modesJ, i);
				if(modeJ) {
					tossModes[i] = json_boolean_value(modeJ) ? TossMode::TOGGLE : TossMode::DIRECT;
				}
				outModes[i] = OutMode::THROUGH;
			}
		} else {
			fromJson(rootJ, "tossModes", tossModes, 1);
			fromJson(rootJ, "outModes", outModes, 2);
		}
	}

	void step() override;

	void onReset() override {
		for(int i = 0; i < 2; i++) {
			tossModes[i] = TossMode::DIRECT;
			outModes[i] = OutMode::GATE;
			tossResults[i] = false;
		}
	}
};


template<typename E>
void cycleMode(E & mode, int end) {
	mode = static_cast<E>((mode + 1) % end);
}


void Branches::step() {
	float gate = 0.0f;
	for(int i = 0; i < 2; i++) {
		switch(modeButtons[i].step(params[MODE1_PARAM + i])) {
			default:
			case LongPressButton::NO_PRESS: break;
			case LongPressButton::SHORT_PRESS: cycleMode(tossModes[i], 2); break;
			case LongPressButton::LONG_PRESS: cycleMode(outModes[i], 3); break;
		}

		if(inputs[IN1_INPUT + i].active) {
			gate = inputs[IN1_INPUT + i].value;
		}
		bool prev_state = gateTriggers[i].isHigh();
		bool triggered = gateTriggers[i].process(gate);
		bool new_state = gateTriggers[i].isHigh();

		if(triggered) { // lo -> hi
			float r = randomUniform();
			float threshold = clamp(params[THRESHOLD1_PARAM + i].value + inputs[P1_INPUT + i].value / 10.f, 0.f, 1.f);
			bool toss = (r < threshold);

			if(tossModes[i] == TossMode::TOGGLE) {
				if(toss) {
					tossResults[i] = !tossResults[i];
				}
			} else {
				tossResults[i] = toss;
			}

			if(outModes[i] != OutMode::THROUGH) {
				outputs[OUT1A_OUTPUT + i].value = tossResults[i] ? 0.0f : 10.0f;
				outputs[OUT1B_OUTPUT + i].value = tossResults[i] ? 10.0f : 0.0f;
			}

			if(toss) {
				lights[STATE1_NEG_LIGHT + 2 * i].value = 1.0f;
			} else {
				lights[STATE1_POS_LIGHT + 2 * i].value = 1.0f;
			}
		} else if(prev_state && !new_state) { // hi -> lo
			if(outModes[i] == OutMode::GATE) {
				outputs[OUT1A_OUTPUT + i].value = 0.0f;
				outputs[OUT1B_OUTPUT + i].value = 0.0f;
			}
		}

		if(outModes[i] == OutMode::THROUGH) {
			outputs[OUT1A_OUTPUT + i].value = tossResults[i] ? 0.0f : gate;
			outputs[OUT1B_OUTPUT + i].value = tossResults[i] ? gate : 0.0f;
		}

		lights[STATE1_POS_LIGHT + 2 * i].value *= 1.0f - engineGetSampleTime() * 15.0f;
		lights[STATE1_NEG_LIGHT + 2 * i].value *= 1.0f - engineGetSampleTime() * 15.0f;
		lights[TOSS_MODE1_LIGHT + i].value = tossModes[i] == TossMode::TOGGLE ? 1.0f : 0.0f;
		lights[OUT_MODE1_POS_LIGHT + 2 * i].value = outModes[i] == OutMode::GATE || outModes[i] == OutMode::LATCH ? 1.0f : 0.0f;
		lights[OUT_MODE1_NEG_LIGHT + 2 * i].value = outModes[i] == OutMode::LATCH || outModes[i] == OutMode::THROUGH ? 1.0f : 0.0f;
	}
}


struct BranchesWidget : ModuleWidget {
	BranchesWidget(Branches * module) : ModuleWidget(module) {
		setPanel(SVG::load(assetPlugin(plugin, "res/Branches.svg")));

		addChild(Widget::create<ScrewSilver>(Vec(15, 0)));
		addChild(Widget::create<ScrewSilver>(Vec(15, 365)));

		addParam(ParamWidget::create<Rogan1PSRed>(Vec(24, 64), module, Branches::THRESHOLD1_PARAM, 0.0f, 1.0f, 0.5f));
		addParam(ParamWidget::create<TL1105>(Vec(69, 58), module, Branches::MODE1_PARAM, 0.0f, 1.0f, 0.0f));
		addInput(Port::create<PJ301MPort>(Vec(9, 122), Port::INPUT, module, Branches::IN1_INPUT));
		addInput(Port::create<PJ301MPort>(Vec(55, 122), Port::INPUT, module, Branches::P1_INPUT));
		addOutput(Port::create<PJ301MPort>(Vec(9, 160), Port::OUTPUT, module, Branches::OUT1A_OUTPUT));
		addOutput(Port::create<PJ301MPort>(Vec(55, 160), Port::OUTPUT, module, Branches::OUT1B_OUTPUT));

		addParam(ParamWidget::create<Rogan1PSGreen>(Vec(24, 220), module, Branches::THRESHOLD2_PARAM, 0.0f, 1.0f, 0.5f));
		addParam(ParamWidget::create<TL1105>(Vec(69, 214), module, Branches::MODE2_PARAM, 0.0f, 1.0f, 0.0f));
		addInput(Port::create<PJ301MPort>(Vec(9, 278), Port::INPUT, module, Branches::IN2_INPUT));
		addInput(Port::create<PJ301MPort>(Vec(55, 278), Port::INPUT, module, Branches::P2_INPUT));
		addOutput(Port::create<PJ301MPort>(Vec(9, 316), Port::OUTPUT, module, Branches::OUT2A_OUTPUT));
		addOutput(Port::create<PJ301MPort>(Vec(55, 316), Port::OUTPUT, module, Branches::OUT2B_OUTPUT));

#if 1
		addChild(ModuleLightWidget::create<SmallLight<GreenLight>>(Vec(58, 56), module, Branches::TOSS_MODE1_LIGHT));
		addChild(ModuleLightWidget::create<SmallLight<GreenLight>>(Vec(58, 210), module, Branches::TOSS_MODE2_LIGHT));
		addChild(ModuleLightWidget::create<SmallLight<GreenRedLight>>(Vec(76, 80), module, Branches::OUT_MODE1_POS_LIGHT));
		addChild(ModuleLightWidget::create<SmallLight<GreenRedLight>>(Vec(76, 236), module, Branches::OUT_MODE2_POS_LIGHT));
#endif

		addChild(ModuleLightWidget::create<SmallLight<GreenRedLight>>(Vec(42, 169), module, Branches::STATE1_POS_LIGHT));
		addChild(ModuleLightWidget::create<SmallLight<GreenRedLight>>(Vec(42, 325), module, Branches::STATE2_POS_LIGHT));
	}

	template<typename E>
	struct ModeItem : MenuItem {
		E * target;
		E mode;
		void onAction(EventAction & e) override {
			*target = mode;
		}
		void step() override {
			rightText = CHECKMARK(*target == mode);
			MenuItem::step();
		}
	};

	void appendContextMenu(Menu * menu) override {
		Branches * branches = dynamic_cast<Branches*>(module);
		assert(branches);

#if 0
		struct TossModeItem : MenuItem {
			Branches * branches;
			int channel;
			void onAction(EventAction & e) override {
				cycleMode(branches->tossModes[channel], 2);
			}
			void step() override {
				if(branches->tossModes[channel] == Branches::TossMode::TOGGLE) {
					rightText = "Toggle";
				} else {
					rightText = "Direct";
				}
				MenuItem::step();
			}
		};

		struct OutModeItem : MenuItem {
			Branches * branches;
			int channel;
			void onAction(EventAction & e) override {
				cycleMode(branches->outModes[channel], 3);
			}
			void step() override {
				if(branches->outModes[channel] == Branches::OutMode::GATE) {
					rightText = "Gate";
				} else if(branches->outModes[channel] == Branches::OutMode::LATCH) {
					rightText = "Latch";
				} else {
					rightText = "Through";
				}
				MenuItem::step();
			}
		};

		menu->addChild(construct<MenuLabel>());
		menu->addChild(construct<TossModeItem>(&MenuItem::text, "Channel 1 Toss Mode", &TossModeItem::branches, branches, &TossModeItem::channel, 0));
		menu->addChild(construct<OutModeItem>(&MenuItem::text, "Channel 1 Output Mode", &OutModeItem::branches, branches, &OutModeItem::channel, 0));
		menu->addChild(construct<TossModeItem>(&MenuItem::text, "Channel 2 Toss Mode", &TossModeItem::branches, branches, &TossModeItem::channel, 1));
		menu->addChild(construct<OutModeItem>(&MenuItem::text, "Channel 2 Output Mode", &OutModeItem::branches, branches, &OutModeItem::channel, 1));

#else

		struct TossModeSub : MenuItem {
			Branches * branches;
			int channel;
			Menu * createChildMenu() override {
				Menu * menu = new Menu();
				using TossModeItem = ModeItem<Branches::TossMode>;
				menu->addChild(construct<TossModeItem>(&MenuItem::text, "Direct", &TossModeItem::target, &branches->tossModes[channel], &TossModeItem::mode, Branches::TossMode::DIRECT));
				menu->addChild(construct<TossModeItem>(&MenuItem::text, "Toggle", &TossModeItem::target, &branches->tossModes[channel], &TossModeItem::mode, Branches::TossMode::TOGGLE));
				return menu;
			}
		};

		struct OutModeSub : MenuItem {
			Branches * branches;
			int channel;
			Menu * createChildMenu() override {
				Menu * menu = new Menu();
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
#endif
	}
};


Model * modelBranches = Model::create<Branches, BranchesWidget>("Audible Instruments", "Branches", "Bernoulli Gate", RANDOM_TAG, DUAL_TAG);
