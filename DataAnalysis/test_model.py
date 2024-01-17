from joblib import load
import numpy as np
import matplotlib.pyplot as plt
import statsmodels.api as sm
from scipy.stats import probplot

def main(forecast_steps=52):
    # Load the model
    model_results = load("sarima_weekly_model.pkl")

    # Print model summary
    print(model_results.summary())

    # Generate forecasts
    forecast = model_results.get_forecast(steps=forecast_steps)
    mean_forecast = forecast.predicted_mean
    confidence_intervals = forecast.conf_int()

    # Plot the forecasts along with historical data
    plt.figure(figsize=(14, 7))
    end_of_history = mean_forecast.index[0]
    historical_data = model_results.data.orig_endog.loc[:end_of_history]
    plt.plot(np.array(historical_data.index), historical_data.values, color='blue', label='Historical Data')
    plt.plot(np.array(mean_forecast.index), mean_forecast.values, color='red', label='Forecast')
    plt.fill_between(np.array(confidence_intervals.index), confidence_intervals.iloc[:, 0], confidence_intervals.iloc[:, 1], color='pink', alpha=0.3, label='95% Prediction Interval')
    plt.title(f"SARIMAX Forecast for Next {forecast_steps} Steps")
    plt.xlabel("Time")
    plt.ylabel("Temperature")
    plt.legend()
    plt.grid(True)
    plt.tight_layout()
    plt.show()

    # Residual Analysis
    residuals = model_results.resid
    fig, axes = plt.subplots(2, 2, figsize=(15, 12))

    # Plotting residuals over time
    axes[0, 0].plot(np.array(residuals.index), residuals.values, color='green')
    axes[0, 0].set_title("Residuals Over Time")

    # Density plot of residuals
    axes[0, 1].hist(residuals, bins=30, density=True, color='blue', alpha=0.7, label='Residuals KDE')
    axes[0, 1].set_title("Density Plot of Residuals")

    # Q-Q plot
    probplot(residuals, plot=axes[1, 0])

    # ACF plot
    sm.graphics.tsa.plot_acf(residuals.values.squeeze(), ax=axes[1, 1])
    plt.tight_layout()
    plt.show()

if __name__ == "__main__":
    steps_to_forecast = int(input("Enter the number of steps you'd like to forecast (e.g., 52 for a year): "))
    main(forecast_steps=steps_to_forecast)
