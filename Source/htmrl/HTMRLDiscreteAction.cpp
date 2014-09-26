/*
AI Lib
Copyright (C) 2014 Eric Laukien

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
claim that you wrote the original software. If you use this software
in a product, an acknowledgment in the product documentation would be
appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
*/

#include <htmrl/HTMRLDiscreteAction.h>

#include <iostream>

using namespace htmrl;

float htmrl::defaultBoostFunctionDiscreteAction(float active, float minimum) {
	return (1.0f - minimum) + std::max(0.0f, -(minimum - active));
}

HTMRLDiscreteAction::HTMRLDiscreteAction()
: _encodeBlobRadius(1), _replaySampleFrames(3), _maxReplayChainSize(600),
_backpropPassesCritic(50), _minibatchSize(16),
_prevMaxQAction(0), _prevChooseAction(0)
{}

void HTMRLDiscreteAction::createRandom(int inputWidth, int inputHeight, int inputDotsWidth, int inputDotsHeight, int condenseWidth, int condenseHeight, int numOutputs, int criticNumHiddenLayers, int criticNumNodesPerHiddenLayer, float criticInitWeightStdDev, const std::vector<RegionDesc> &regionDescs, std::mt19937 &generator) {
	_inputWidth = inputWidth;
	_inputHeight = inputHeight;

	_inputDotsWidth = inputDotsWidth;
	_inputDotsHeight = inputDotsHeight;

	_condenseWidth = condenseWidth;
	_condenseHeight = condenseHeight;

	_inputMax = _inputDotsWidth * _inputDotsHeight;

	_regionDescs = regionDescs;

	_inputf.resize(_inputWidth * _inputHeight * 2);
	_inputb.resize(_inputWidth * _inputHeight * _inputMax);

	for (int i = 0; i < _inputf.size(); i++)
		_inputf[i] = 0.0f;

	int dotsWidth = _inputWidth * _inputDotsWidth;
	int dotsHeight = _inputHeight * _inputDotsHeight;

	_regions.resize(_regionDescs.size());

	for (int i = 0; i < _regions.size(); i++) {
		_regions[i].createRandom(dotsWidth, dotsHeight, _regionDescs[i]._connectionRadius, _regionDescs[i]._initInhibitionRadius,
			_regionDescs[i]._initNumSegments, _regionDescs[i]._regionWidth, _regionDescs[i]._regionHeight, _regionDescs[i]._columnSize,
			_regionDescs[i]._permanenceDistanceBias, _regionDescs[i]._permanenceDistanceFalloff, _regionDescs[i]._permanenceBiasFloor,
			_regionDescs[i]._connectionPermanenceTarget, _regionDescs[i]._connectionPermanenceStdDev, generator);

		dotsWidth = _regionDescs[i]._regionWidth;
		dotsHeight = _regionDescs[i]._regionHeight;
	}

	_condenseBufferWidth = std::ceil(static_cast<float>(dotsWidth) / _condenseWidth);
	_condenseBufferHeight = std::ceil(static_cast<float>(dotsHeight) / _condenseHeight);

	int stateSize = _condenseBufferWidth * _condenseBufferHeight;

	_inputCond.clear();
	_inputCond.assign(stateSize, 0.0f);

	_critic.createRandom(stateSize, numOutputs, criticNumHiddenLayers, criticNumNodesPerHiddenLayer, criticInitWeightStdDev, generator);

	_prevLayerInputf.clear();
	_prevLayerInputf.assign(stateSize, false);

	_prevQValues.clear();
	_prevQValues.assign(_critic.getNumOutputs(), 0.0f);
}

void HTMRLDiscreteAction::decodeInput() {
	for (int i = 0; i < _inputb.size(); i++)
		_inputb[i] = false;

	int inputBWidth = _inputWidth * _inputDotsWidth;

	for (int x = 0; x < _inputWidth; x++)
	for (int y = 0; y < _inputHeight; y++) {
		int bX = x * _inputDotsWidth;
		int bY = y * _inputDotsHeight;

		int eX = bX + _inputDotsWidth;
		int eY = bY + _inputDotsHeight;

		int numDotsX = static_cast<int>((_inputf[x + y * _inputWidth + 0 * _inputWidth * _inputHeight] * 0.5f + 0.5f) * _inputDotsWidth);
		int numDotsY = static_cast<int>((_inputf[x + y * _inputWidth + 1 * _inputWidth * _inputHeight] * 0.5f + 0.5f) * _inputDotsHeight);

		int dotX = bX + numDotsX;
		int dotY = bY + numDotsY;

		for (int dx = -_encodeBlobRadius; dx <= _encodeBlobRadius; dx++)
		for (int dy = -_encodeBlobRadius; dy <= _encodeBlobRadius; dy++) {
			int pX = dotX + dx;
			int pY = dotY + dy;

			if (pX >= bX && pX < eX && pY >= bY && pY < eY)
				_inputb[pX + pY * inputBWidth] = true;
		}
	}
}

int HTMRLDiscreteAction::step(float reward, float qAlpha, float backpropAlphaCritic, float rmsDecayCritic, float momentumCritic, float gamma, float lambda, float tauInv, float epsilon, float weightDecayMultiplier, std::mt19937 &generator, std::vector<float> &condensed) {
	decodeInput();

	std::vector<bool> layerInput = _inputb;

	int dotsWidth = _inputWidth * _inputDotsWidth;
	int dotsHeight = _inputHeight * _inputDotsHeight;

	for (int i = 0; i < _regions.size(); i++) {
		_regions[i].stepBegin();

		_regions[i].spatialPooling(layerInput, _regionDescs[i]._minPermanence, _regionDescs[i]._minOverlap, _regionDescs[i]._desiredLocalActivity,
			_regionDescs[i]._spatialPermanenceIncrease, _regionDescs[i]._spatialPermanenceDecrease, _regionDescs[i]._minDutyCycleRatio, _regionDescs[i]._activeDutyCycleDecay,
			_regionDescs[i]._overlapDutyCycleDecay, _regionDescs[i]._subOverlapPermanenceIncrease, _regionDescs[i]._boostFunction);

		_regions[i].temporalPoolingLearn(_regionDescs[i]._minPermanence, _regionDescs[i]._learningRadius, _regionDescs[i]._minLearningThreshold,
			_regionDescs[i]._activationThreshold, _regionDescs[i]._newNumConnections, _regionDescs[i]._temporalPermanenceIncrease,
			_regionDescs[i]._temporalPermanenceDecrease, _regionDescs[i]._newConnectionPermanence, _regionDescs[i]._maxSteps, generator);

		layerInput.resize(_regions[i].getRegionWidth() * _regions[i].getRegionHeight());

		for (int x = 0; x < _regions[i].getRegionWidth(); x++)
		for (int y = 0; y < _regions[i].getRegionHeight(); y++)
			layerInput[x + y * _regions[i].getRegionWidth()] = _regions[i].getOutput(x, y);

		dotsWidth = _regionDescs[i]._regionWidth;
		dotsHeight = _regionDescs[i]._regionHeight;
	}

	// Condense
	std::vector<float> condensedInputf(_condenseBufferWidth * _condenseBufferHeight);

	float maxInv = 1.0f / (_condenseWidth * _condenseHeight);
	
	for (int x = 0; x < _condenseBufferWidth; x++)
	for (int y = 0; y < _condenseBufferHeight; y++) {

		float sum = 0.0f;

		for (int dx = 0; dx < _condenseWidth; dx++)
		for (int dy = 0; dy < _condenseHeight; dy++) {
			int bX = x * _condenseWidth + dx;
			int bY = y * _condenseHeight + dy;

			if (bX >= 0 && bX < dotsWidth && bY >= 0 && bY < dotsHeight)
				sum += (layerInput[bX + bY * dotsWidth] ? 1.0f : 0.0f);
		}

		sum *= maxInv;

		condensedInputf[x + y * _condenseBufferWidth] = sum;
	}

	condensed = condensedInputf;

	// Get maximum Q prediction from critic
	std::vector<float> criticOutput(_critic.getNumOutputs());

	_critic.process(condensedInputf, criticOutput);

	int maxQActionIndex = 0;

	for (int i = 1; i < _critic.getNumOutputs(); i++) {
		if (criticOutput[i] > criticOutput[maxQActionIndex])
			maxQActionIndex = i;
	}

	float nextMaxQ = criticOutput[maxQActionIndex];

	// Generate exploratory action
	std::uniform_real_distribution<float> uniformDist(0.0f, 1.0f);

	float newAdv = _prevQValues[_prevMaxQAction] + (reward + gamma * nextMaxQ - _prevQValues[_prevMaxQAction]) * tauInv;

	float errorCritic = qAlpha * (newAdv - _prevQValues[_prevChooseAction]);

	// Add sample to chain
	ReplaySample sample;
	sample._actorInputsf = _prevLayerInputf;

	sample._actionExploratory = _prevChooseAction;
	sample._actionOptimal = _prevMaxQAction;

	sample._reward = reward;

	sample._actionQValues = _prevQValues;

	sample._actionQValues[_prevChooseAction] += errorCritic;

	float prevV = sample._actionQValues[0];

	for (int i = 1; i < sample._actionQValues.size(); i++)
		prevV = std::max(prevV, sample._actionQValues[i]);

	float g = gamma * lambda;

	for (std::list<ReplaySample>::iterator it = _replayChain.begin(); it != _replayChain.end(); it++) {
		//float value = it->_actionQValues[it->_actionOptimal] + (it->_reward + gamma * prevV - it->_actionQValues[it->_actionOptimal]) * tauInv;
		float error = g * errorCritic; // (value - it->_actionQValues[it->_actionExploratory])

		it->_actionQValues[it->_actionExploratory] += error;

		it->_actionOptimal = 0;

		for (int i = 1; i < it->_actionQValues.size(); i++)
		if (it->_actionQValues[i] > it->_actionQValues[it->_actionOptimal])
			it->_actionOptimal = i;

		prevV = it->_actionQValues[it->_actionOptimal];

		g *= gamma * lambda;
	}

	_replayChain.push_front(sample);

	while (_replayChain.size() > _maxReplayChainSize)
		_replayChain.pop_back();

	// Get random access to samples
	std::vector<ReplaySample*> pReplaySamples(_replayChain.size());

	int index = 0;

	for (std::list<ReplaySample>::iterator it = _replayChain.begin(); it != _replayChain.end(); it++, index++)
		pReplaySamples[index] = &(*it);

	// Rehearse
	std::uniform_int_distribution<int> sampleDist(0, pReplaySamples.size() - 1);

	std::vector<float> inputf(_critic.getNumInputs());
	std::vector<float> criticTempOutput(_critic.getNumOutputs());

	std::vector<float> criticTarget(_critic.getNumOutputs());

	//_critic.decayWeights(weightDecayMultiplier);

	float minibatchInv = 1.0f / _minibatchSize;

	for (int s = 0; s < _backpropPassesCritic; s++) {
		_critic.clearGradient();

		for (int b = 0; b < _minibatchSize; b++) {
			int replayIndex = sampleDist(generator);

			ReplaySample* pSample = pReplaySamples[replayIndex];

			for (int i = 0; i < _critic.getNumInputs(); i++)
				inputf[i] = pSample->_actorInputsf[i];

			_critic.process(inputf, criticTempOutput);

			criticTarget = criticTempOutput;

			criticTarget[pSample->_actionExploratory] = pSample->_actionQValues[pSample->_actionExploratory];

			_critic.accumulateGradient(inputf, criticTarget);
		}

		_critic.scaleGradient(minibatchInv);

		_critic.moveAlongGradientRMS(rmsDecayCritic, backpropAlphaCritic, momentumCritic);
	}

	_critic.process(condensedInputf, criticOutput);

	maxQActionIndex = 0;

	for (int i = 1; i < _critic.getNumOutputs(); i++) {
		if (criticOutput[i] > criticOutput[maxQActionIndex])
			maxQActionIndex = i;
	}

	int choosenAction;

	if (uniformDist(generator) < epsilon) {
		std::uniform_int_distribution<int> actionDist(0, _critic.getNumOutputs() - 1);

		choosenAction = actionDist(generator);
	}
	else
		choosenAction = maxQActionIndex;

	_prevMaxQAction = maxQActionIndex;
	_prevChooseAction = choosenAction;

	_prevQValues = criticOutput;

	_prevLayerInputf = condensedInputf;

	std::cout << errorCritic << " " << _prevQValues[_prevMaxQAction] << " " << _prevQValues[_prevChooseAction] << std::endl;

	return choosenAction;
}