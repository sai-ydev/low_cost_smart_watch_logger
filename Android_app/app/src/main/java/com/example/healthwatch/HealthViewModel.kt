package com.example.healthwatch

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch

class HealthViewModel(application: Application) : AndroidViewModel(application) {

    private val bleManager = BleManager(application.applicationContext)

    // Expose state flows to the UI
    val bleState:   StateFlow<BleState>   = bleManager.bleState
    val healthData: StateFlow<HealthData> = bleManager.healthData

    fun connect() {
        viewModelScope.launch {
            bleManager.startScan()
        }
    }

    fun disconnect() {
        bleManager.disconnect()
    }

    fun syncTime() {
        bleManager.syncTime()
    }

    override fun onCleared() {
        super.onCleared()
        bleManager.disconnect()
    }
}