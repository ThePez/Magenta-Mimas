using UnityEngine;
using UnityEngine.Rendering.HighDefinition;

[RequireComponent(typeof(Rigidbody))]
public class SimpleBuoyancy : MonoBehaviour
{
    public float floatHeight = 0.5f;     // how much above the water surface
    public float bounceDamping = 0.1f;   // smoothing vertical velocity
    public float waterDrag = 0.2f;       // drag when in water

    private Rigidbody rb;
    private WaterSurface waterSurface;

    void Start()
    {
        rb = GetComponent<Rigidbody>();

        GameObject oceanObj = GameObject.Find("Ocean");
        if (!oceanObj)
        {
            return;
        }

        waterSurface = oceanObj.GetComponent<WaterSurface>();
    }

    void FixedUpdate()
    {
        if (!waterSurface)
        {
            return;
        }
        
        WaterSearchParameters searchParams = new WaterSearchParameters
        {
            // Set up the search parameters
            startPositionWS = transform.position,
            targetPositionWS = transform.position + Vector3.up * 2f,
            error = 0.01f,
            
            // Optionally set maxIterations if needed (older versions require)
            maxIterations = 8
        };
        
        // Query the water surface
        if (!waterSurface.ProjectPointOnWaterSurface(searchParams, out var searchResult))
        {
            return;
        }

        float depth = searchResult.projectedPositionWS.y - transform.position.y + floatHeight;

        if (depth > 0f)
        {
            // Buoyancy force
            float buoyancyForce = Physics.gravity.magnitude * rb.mass * depth;
            rb.AddForce(Vector3.up * buoyancyForce, ForceMode.Acceleration);

            // Water drag
            rb.AddForce(-rb.linearVelocity * waterDrag, ForceMode.Acceleration);
        }

        // Vertical damping to reduce jitter
        rb.AddForce(Vector3.up * (-rb.linearVelocity.y * bounceDamping), ForceMode.Acceleration);
    }
}